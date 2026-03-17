#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <syslog.h>
#include <unistd.h>
#include <time.h>
#include <math.h>
#include <sys/stat.h>
#include <errno.h>
#include <string.h>

#include "../sdk/ss_sdk.h"
#include "../libs/json/cJSON.h"

#include "compute_models.h"
#include "algorithms/compute_heuristic.h"
#include "algorithms/compute_lp.h"

#define ENDPOINTS_DIR "endpoints"
#define ENDPOINTS_RESULT_FILE "endpoints/result.json"
#define ENDPOINTS_FORECAST_FILE "endpoints/forecast.json"
#define SLOT_SECONDS 900

int g_compute_method = 0;
int g_exp_backoff_poll_rate = 3;
int g_exp_backoff_timeout = 5 * 60;

void cleanup(void);

//          FORWARD DECLARATIONS           //
//*****************************************//

// Environment variables
int fetch_env_vars();

// Load data
int wait_for_new_data();
void init_compute_data(compute_data_t* data);
int count_horizon_len_from_inputs(const compute_data_t* data, int max_len);
int save_forecast(const compute_data_t* data, int horizon_len);
int load_data(compute_data_t* out);

// Compute
void init_result_data(result_t* result);
int compute(compute_data_t* data, result_t* out_result);

// Save result
int add_series_to_json(cJSON* parent, const char* name, const double* values, int valid_len);
int add_int64_series_to_json(cJSON* parent, const char* name, const int64_t* values, int valid_len);
int save_result(const result_t* result, int horizon_len);

//          MAIN           //
//*************************//

int main() {
    atexit(cleanup);
    openlog("SUNSPOTS_COMPUTE_MANAGER", LOG_PID, LOG_DAEMON);

    syslog(LOG_INFO, "Compute Manager - Starting...");

    compute_data_t data;
    result_t result = {0};

    if (fetch_env_vars() < 0) {
        syslog(LOG_ERR, "Compute Manager - Unable to fetch environment variables.");
        return EXIT_FAILURE;
    }

    if (wait_for_new_data() < 0) {
        syslog(LOG_ERR, "Compute Manager - No new data detected.");
        return EXIT_FAILURE;
    }

    if (load_data(&data) < 0) {
        syslog(LOG_ERR, "Compute Manager - Loading data failed.");
        return EXIT_FAILURE;
    }

    if (compute(&data, &result) < 0) {
        syslog(LOG_ERR, "Compute Manager - Compute failed.");
        return EXIT_FAILURE;
    }

    if (save_result(&result, data.horizon_len) < 0) {
        syslog(LOG_ERR, "Compute Manager - Saving result failed.");
        return EXIT_FAILURE;
    }

    syslog(LOG_INFO, "Compute Manager - Done.");
    return EXIT_SUCCESS;
}

void cleanup(void) {
    syslog(LOG_INFO, "Compute Manager - Terminating.");
    closelog();
}

int fetch_env_vars() {
    const char* blob_config = getenv("SUNSPOTS_CONFIG");

    if (blob_config == NULL || blob_config[0] == '\0') {
        syslog(LOG_WARNING, "Compute Manager - SUNSPOTS_CONFIG missing.");
        return -1;
    }

    cJSON* root = cJSON_Parse(blob_config);
    if (!root) {
        syslog(LOG_WARNING, "Compute Manager - Invalid SUNSPOTS_CONFIG JSON.");
        return -1;
    }

    cJSON* config_obj = cJSON_GetObjectItemCaseSensitive(root, "system");
    if (!cJSON_IsObject(config_obj)) {
        config_obj = root;
    }

    cJSON* method_obj = cJSON_GetObjectItemCaseSensitive(config_obj, "compute_method");
    cJSON* exp_backoff_poll_rate_obj = cJSON_GetObjectItemCaseSensitive(config_obj, "exp_backoff_poll_rate");
    cJSON* exp_backoff_timeout_obj = cJSON_GetObjectItemCaseSensitive(config_obj, "exp_backoff_timeout");

    if (!cJSON_IsNumber(method_obj)) {
        syslog(LOG_WARNING, "Compute Manager - Missing/invalid compute_method.");
        cJSON_Delete(root);
        return -1;
    }

    if (!cJSON_IsNumber(exp_backoff_poll_rate_obj)) {
        syslog(LOG_WARNING, "Compute Manager - Missing/invalid exp_backoff_poll_rate.");
        cJSON_Delete(root);
        return -1;
    }

    if (!cJSON_IsNumber(exp_backoff_timeout_obj)) {
        syslog(LOG_WARNING, "Compute Manager - Missing/invalid exp_backoff_timeout.");
        cJSON_Delete(root);
        return -1;
    }

    double method_d = method_obj->valuedouble;
    double poll_rate_d = exp_backoff_poll_rate_obj->valuedouble;
    double timeout_d = exp_backoff_timeout_obj->valuedouble;

    /* Enforce integer-only config values */
    if (method_d != (double)(int)method_d ||
        poll_rate_d != (double)(int)poll_rate_d ||
        timeout_d != (double)(int)timeout_d) {
        syslog(LOG_WARNING, "Compute Manager - compute_method, exp_backoff_poll_rate and exp_backoff_timeout must be integers.");
        cJSON_Delete(root);
        return -1;
    }

    int method = (int)method_d;
    int poll_rate = (int)poll_rate_d;
    int timeout = (int)timeout_d;

    if (method < 0 || method > 1) {
        syslog(LOG_WARNING, "Compute Manager - compute_method out of range: %d", method);
        cJSON_Delete(root);
        return -1;
    }

    if (poll_rate < 1) {
        syslog(LOG_WARNING, "Compute Manager - poll_rate must be > 0 (got %d)", poll_rate);
        cJSON_Delete(root);
        return -1;
    }

    if (timeout < 300) {
        syslog(LOG_WARNING, "Compute Manager - exp_backoff_timeout must be >= 300 (got %d)", timeout);
        cJSON_Delete(root);
        return -1;
    }

    if (timeout < poll_rate) {
        syslog(LOG_WARNING, "Compute Manager - exp_backoff_timeout (%d) must be >= exp_backoff_poll_rate (%d)", timeout, poll_rate);
        cJSON_Delete(root);
        return -1;
    }

    g_compute_method = method;
    g_exp_backoff_poll_rate = poll_rate;
    g_exp_backoff_timeout = timeout;

    syslog(LOG_INFO, "Compute Manager - Config loaded: compute_method=%d exp_backoff_poll_rate=%d exp_backoff_timeout=%d", g_compute_method, g_exp_backoff_poll_rate, g_exp_backoff_timeout);

    cJSON_Delete(root);
    return 0;
}

//          LOAD DATA           //
//******************************//

int wait_for_new_data() {
    const int max_cycles = g_exp_backoff_timeout / g_exp_backoff_poll_rate;

    int cycles = 0;

    while (cycles < max_cycles) {
        syslog(LOG_INFO, "Compute Manager - Looking for fresh data...");

        const int64_t now = (int64_t)time(NULL);
        const int64_t window_start = now - 90 * 60;
        const int64_t window_end = now + 90 * 60;

        ss_metric_id metrics[3] = {
            // SS_METRIC_WEATHER_RADIATION_SHORTWAVE_WM2,
            SS_METRIC_WEATHER_CLOUD_COVER_TOTAL_PCT,
            SS_METRIC_WEATHER_TEMPERATURE_AIR_2M_C
            // SS_METRIC_ENERGY_PRICE_SPOT_SEK_KWH
        };

        int observed_metrics_found = 0;

        for (int m = 0; m < 2; m++) {
            int metric_has_observed = 0;

            ss_sdk_samples_out samples = {0};
            ss_sdk_status status = ss_sdk_db_get_canonical(window_start, 13, metrics[m], &samples);

            if (status != SS_SDK_OK && status != SS_SDK_ERR_PARTIAL_DATA) {
                syslog(LOG_ERR, "Compute Manager - ss_sdk_db_get_canonical_failed for metric=%d status=%d", (int)metrics[m], (int)status);
                return -1;
            }

            for (size_t i = 0; i < samples.count; i++) {
                const ss_sdk_sample* s = &samples.samples[i];
                if (s->value_type != SS_SDK_VALUE_F64) continue;
                if (s->ts_utc < window_start || s->ts_utc > window_end) continue;

                if ((s->flags & SS_SDK_SAMPLE_OBSERVED) != 0) {
                    metric_has_observed = 1;
                    break;
                }
            }

            ss_sdk_db_free_samples(&samples);

            if (metric_has_observed) {
                observed_metrics_found++;
            }
        }

        if (observed_metrics_found == 2) {
            syslog(LOG_INFO, "Compute Manager - Fresh data found!");
            return 0;
        }

        cycles++;
        sleep(g_exp_backoff_poll_rate);
    }

    syslog(LOG_WARNING, "Compute Manager - Timeout waiting for new observed weather data.");
    return -1;
}

void init_compute_data(compute_data_t* data) {
    for (int i = 0; i < SERIES_LEN; i++) {
        data->irradiance[i] = NAN;
        data->cloudiness[i] = NAN;
        data->temperature[i] = NAN;
        data->price_kwh[i] = NAN;
        data->timestamp[i] = 0;
    }
}

int count_horizon_len_from_inputs(const compute_data_t* data, int max_len) {
    int len;

    if (!data) return 0;
    if (max_len < 0) max_len = 0;
    if (max_len > SERIES_LEN) max_len = SERIES_LEN;

    len = 0;
    for (int i = 0; i < max_len; i++) {
        if (isnan(data->irradiance[i]) ||
            isnan(data->cloudiness[i]) ||
            isnan(data->temperature[i]) ||
            isnan(data->price_kwh[i])) {
            break;
        }
        len++;
    }

    return len;
}

int save_forecast(const compute_data_t* data, int horizon_len) {
    if (!data) return -1;

    if (mkdir(ENDPOINTS_DIR, 0755) < 0 && errno != EEXIST) {
        int err = errno;
        syslog(LOG_ERR, "Compute Manager - mkdir('%s') failed %s", ENDPOINTS_DIR, strerror(err));
        return -1;
    }

    cJSON* root = cJSON_CreateObject();
    if (!root) return -1;

    cJSON* forecast_obj = cJSON_CreateObject();
    if (!forecast_obj) {
        cJSON_Delete(root);
        return -1;
    }

    if (add_series_to_json(forecast_obj, "irradiance", data->irradiance, horizon_len) < 0 ||
        add_series_to_json(forecast_obj, "cloudiness", data->cloudiness, horizon_len) < 0 ||
        add_series_to_json(forecast_obj, "temperature", data->temperature, horizon_len) < 0 ||
        add_int64_series_to_json(forecast_obj, "timestamp", data->timestamp, horizon_len)) {
        cJSON_Delete(forecast_obj);
        cJSON_Delete(root);
        return -1;
    }

    cJSON_AddItemToObject(root, "forecast", forecast_obj);

    char* json = cJSON_Print(root);
    cJSON_Delete(root);
    if (!json) return -1;

    FILE* f = fopen(ENDPOINTS_FORECAST_FILE, "w");
    if (!f) {
        int err = errno;
        syslog(LOG_ERR, "Compute Manager - fopen('%s') failed: %s", ENDPOINTS_FORECAST_FILE, strerror(err));
        free(json);
        return -1;
    }

    fputs(json, f);
    fputc('\n', f);
    fclose(f);
    free(json);

    return 0;
}

void log_time(const char *label, time_t t)
{
    char buf[64];
    struct tm tm;

    localtime_r(&t, &tm);

    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm);

    syslog(LOG_INFO, "%s: %s", label, buf);
}

int load_data(compute_data_t* out) {
    if (!out) return -1;

    init_compute_data(out);

    struct tm local_tm;
    const int64_t now = (int64_t)time(NULL);
    localtime_r(&now, &local_tm);

    local_tm.tm_hour = 0;
    local_tm.tm_min = 0;
    local_tm.tm_sec = 0;
    local_tm.tm_mday += 1;

    const int64_t start_slot = (now / SLOT_SECONDS) * SLOT_SECONDS;
    int64_t end_slot = (int64_t)mktime(&local_tm);
    end_slot = start_slot + ((60 * 60) * 24);

    // time_t start = start_slot;
    // time_t end = end_slot;

    // log_time("start_slot", start);
    // log_time("end_slot", end);

    int horizon_slots = (int)((end_slot - start_slot) / SLOT_SECONDS);
    if (horizon_slots < 0) horizon_slots = 0;
    if (horizon_slots > SERIES_LEN) horizon_slots = SERIES_LEN;

    // syslog(LOG_INFO, "horizon_slots: %d", horizon_slots);

    ss_metric_id metrics[4] =  {
        SS_METRIC_WEATHER_RADIATION_SHORTWAVE_WM2,
        SS_METRIC_WEATHER_CLOUD_COVER_TOTAL_PCT,
        SS_METRIC_WEATHER_TEMPERATURE_AIR_2M_C,
        SS_METRIC_ENERGY_PRICE_SPOT_SEK_KWH
    };

    double* targets[4] = {
        out->irradiance,
        out->cloudiness,
        out->temperature,
        out->price_kwh
    };

    for (int m = 0; m < 4; m++) {
        ss_sdk_samples_out samples = {0};
        ss_sdk_status status = ss_sdk_db_get_canonical(0, 0, metrics[m], &samples);

        if (status != SS_SDK_OK && status != SS_SDK_ERR_PARTIAL_DATA) {
            syslog(LOG_ERR, "Compute Manager - ss_sdk_db_get_canonical failed for metric=%d status=%d", (int)metrics[m], (int)status);
            return -1;
        }

        // syslog(LOG_INFO, "Samples found: %zu", samples.count);

        for (size_t i = 0; i < samples.count; i++) {
            const ss_sdk_sample* s = &samples.samples[i];

            if (s->value_type != SS_SDK_VALUE_F64) continue;
            if (s->ts_utc < start_slot || s->ts_utc >= end_slot) continue;

            int idx = (int)((s->ts_utc - start_slot) / SLOT_SECONDS);
            if (idx >= 0 && idx < horizon_slots) {
                targets[m][idx] = s->value.f64;

                if (m == 3) {
                    out->timestamp[idx] = s->ts_utc;
                }
            }
        }

        ss_sdk_db_free_samples(&samples);
    }

    out->horizon_len = count_horizon_len_from_inputs(out, horizon_slots);

    // syslog(LOG_INFO, "horizon_len: %d", out->horizon_len);

    if (out->horizon_len <= 0) {
        syslog(LOG_ERR, "Compute Manager - No usable elpris slots.");
        return -1;
    }

    if (save_forecast(out, out->horizon_len) < 0) {
        syslog(LOG_ERR, "Compute Manager - Failed to save forecast data to endpoints folder.");
        return -1;
    }

    return 0;
}

//          COMPUTE           //
//****************************//

void init_result_data(result_t* result) {
    for (int i = 0; i < SERIES_LEN; i++) {
        result->buy_electricity[i] = 0.0;
        result->direct_use[i] = 0.0;
        result->charge_battery[i] = 0.0;
        result->sell_excess[i] = 0.0;
        result->timestamp[i] = 0;
    }
}

int compute(compute_data_t* data, result_t* out_result) {
    if (!data || !out_result) return -1;

    init_result_data(out_result);

    switch (g_compute_method) {
        case 0:
            if (compute_heuristic(data, out_result) < 0) {
                return -1;
            }
            break;
        case 1:
            if (compute_lp(data, out_result) < 0) {
                return -1;
            }
            break;
    }
    
    for (int i = 0; i < data->horizon_len; i++) {
        out_result->timestamp[i] = data->timestamp[i];
    }

    return 0;
}

//          SAVE RESULT           //
//********************************//

int add_series_to_json(cJSON* parent, const char* name, const double* values, int valid_len) {
    cJSON* array = cJSON_CreateArray();
    if (!array) {
        return -1;
    }

    if (valid_len < 0) valid_len = 0;
    if (valid_len > SERIES_LEN) valid_len = SERIES_LEN;

    for (int i = 0; i < SERIES_LEN; i++) {
        cJSON* value_item;

        if (i >= valid_len) {
            value_item = cJSON_CreateNull();
        } else {
            value_item = cJSON_CreateNumber(values[i]);
        }

        if (!value_item) {
            cJSON_Delete(array);
            return -1;
        }
        cJSON_AddItemToArray(array, value_item);
    }

    cJSON_AddItemToObject(parent, name, array);
    return 0;
}

int add_int64_series_to_json(cJSON* parent, const char* name, const int64_t* values, int valid_len) {
    cJSON* array = cJSON_CreateArray();
    if (!array) {
        return -1;
    }

    if (valid_len < 0) valid_len = 0;
    if (valid_len > SERIES_LEN) valid_len = SERIES_LEN;

    for (int i = 0; i < SERIES_LEN; i++) {
        cJSON* value_item;

        if (i >= valid_len) {
            value_item = cJSON_CreateNull();
        } else {
            value_item = cJSON_CreateNumber(values[i]);
        }

        if (!value_item) {
            cJSON_Delete(array);
            return -1;
        }
        cJSON_AddItemToArray(array, value_item);
    }

    cJSON_AddItemToObject(parent, name, array);
    return 0;
}

int save_result(const result_t* result, int horizon_len) {
    if (!result) return -1;

    if (mkdir(ENDPOINTS_DIR, 0755) < 0 && errno != EEXIST) {
        int err = errno;
        syslog(LOG_ERR, "Compute Manager - mkdir('%s') failed %s", ENDPOINTS_DIR, strerror(err));
        return -1;
    }

    cJSON* root = cJSON_CreateObject();
    if (!root) return -1;

    cJSON* result_obj = cJSON_CreateObject();
    if (!result_obj) {
        cJSON_Delete(root);
        return -1;
    }

    if (add_series_to_json(result_obj, "buy_electricity", result->buy_electricity, horizon_len) < 0 ||
        add_series_to_json(result_obj, "direct_use", result->direct_use, horizon_len) < 0 ||
        add_series_to_json(result_obj, "charge_battery", result->charge_battery, horizon_len) < 0 ||
        add_series_to_json(result_obj, "sell_excess", result->sell_excess, horizon_len) < 0 ||
        add_int64_series_to_json(result_obj, "timestamp", result->timestamp, horizon_len) < 0) {
        cJSON_Delete(result_obj);
        cJSON_Delete(root);
        return -1;
    }

    cJSON_AddItemToObject(root, "result", result_obj);

    char* json = cJSON_Print(root);
    cJSON_Delete(root);
    if (!json) return -1;

    FILE* f = fopen(ENDPOINTS_RESULT_FILE, "w");
    if (!f) {
        int err = errno;
        syslog(LOG_ERR, "Compute Manager - fopen('%s') failed: %s", ENDPOINTS_RESULT_FILE, strerror(err));
        free(json);
        return -1;
    }

    fputs(json, f);
    fputc('\n', f);
    fclose(f);
    free(json);

    return 0;
}
