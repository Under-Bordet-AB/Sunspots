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

#include "algorithms/compute_lp.h"

#define ENDPOINTS_DIR "endpoints"
#define ENDPOINTS_FILE "endpoints/result.json"
#define SERIES_LEN 96
#define SLOT_SECONDS 900

typedef struct compute_data_t {
    double irradiance[SERIES_LEN];
    double cloudiness[SERIES_LEN];
    double temperature[SERIES_LEN];

    double elpris[SERIES_LEN];
} compute_data_t;

typedef struct result_t {
    double buy_electricity;
    double direct_use;
    double charge_battery;
    double sell_excess;
} result_t;

void cleanup(void);
int load_data(compute_data_t* out);
int compute(const compute_data_t* data, result_t* out_result);
int save_result(const result_t* result);

int main() {
    atexit(cleanup);
    openlog("SUNSPOTS_COMPUTE_MANAGER", LOG_PID, LOG_DAEMON);

    syslog(LOG_INFO, "Compute Manager - Starting...");

    compute_data_t data;
    result_t result = {0};

    if (load_data(&data) < 0) {
        syslog(LOG_ERR, "Compute Manager - Loading data failed.");
        return EXIT_FAILURE;
    }

    if (compute(&data, &result) < 0) {
        syslog(LOG_ERR, "Compute Manager - Compute failed.");
        return EXIT_FAILURE;
    }

    if (save_result(&result) < 0) {
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

static void init_compute_data(compute_data_t* data) {
    for (int i = 0; i < SERIES_LEN; i++) {
        data->irradiance[i] = NAN;
        data->cloudiness[i] = NAN;
        data->temperature[i] = NAN;
        data->elpris[i] = NAN;
    }
}

int load_data(compute_data_t* out) {
    if (!out) return -1;

    init_compute_data(out);

    const int64_t start_slot = ((int64_t)time(NULL) / SLOT_SECONDS) * SLOT_SECONDS;
    const int64_t end_slot = start_slot + ((int64_t)SERIES_LEN * SLOT_SECONDS);

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
        out->elpris
    };

    for (int m = 0; m < 4; m++) {
        ss_sdk_samples_out samples = {0};
        ss_sdk_status status = ss_sdk_db_get_canonical(0, SERIES_LEN, metrics[m], &samples);

        if (status != SS_SDK_OK && status != SS_SDK_ERR_PARTIAL_DATA) {
            syslog(LOG_ERR, "Compute Manager - ss_sdk_db_get_canonical failed for metric=%d status=%d", (int)metrics[m], (int)status);
            return -1;
        }

        for (size_t i = 0; i < samples.count; i++) {
            const ss_sdk_sample* s = &samples.samples[i];

            if (s->value_type != SS_SDK_VALUE_F64) continue;
            if (s->ts_utc < start_slot || s->ts_utc >= end_slot) continue;

            int idx = (int)((s->ts_utc - start_slot) / SLOT_SECONDS);
            if (idx >= 0 && idx < SERIES_LEN) {
                targets[m][idx] = s->value.f64;
            }
        }

        ss_sdk_db_free_samples(&samples);
    }

    return 0;
}

int compute(const compute_data_t* data, result_t* out_result) {
    if (!data || !out_result) return -1;

    // LP function here

    out_result->buy_electricity = 0.0;
    out_result->direct_use = 0.0;
    out_result->charge_battery = 0.0;
    out_result->sell_excess = 0.0;

    return 0;
}

int save_result(const result_t* result) {
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

    cJSON_AddNumberToObject(result_obj, "buy_electricity", result->buy_electricity);
    cJSON_AddNumberToObject(result_obj, "direct_use", result->direct_use);
    cJSON_AddNumberToObject(result_obj, "charge_battery", result->charge_battery);
    cJSON_AddNumberToObject(result_obj, "sell_excess", result->sell_excess);
    cJSON_AddNumberToObject(result_obj, "timestamp", (double)time(NULL));

    cJSON_AddItemToObject(root, "result", result_obj);

    char* json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!json) return -1;

    FILE* f = fopen(ENDPOINTS_FILE, "w");
    if (!f) {
        int err = errno;
        syslog(LOG_ERR, "Compute Manager - fopen('%s') failed: %s", ENDPOINTS_FILE, strerror(err));
        free(json);
        return -1;
    }

    fputs(json, f);
    fputc('\n', f);
    fclose(f);
    free(json);

    return 0;
}