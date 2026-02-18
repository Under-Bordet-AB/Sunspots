#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <curl/curl.h>
#include <cJSON.h>
#include <math.h>
#include <signal.h>
#include <pthread.h>

#include "sdk/ss_sdk.h"

/* Configuration structure */
typedef struct {
    char name[64];
    double latitude;
    double longitude;
} location_t;

typedef struct {
    int enabled;
    location_t *locations;
    int num_locations;
    int forecast_horizon_hours;
    int slot_interval_minutes;
    int fetch_interval_seconds;
    int request_delay_ms;
    int batch_size;
    int retry_on_error;
    int max_retries;
} backfiller_config_t;

/* Statistics */
typedef struct {
    int64_t start_time;
    int64_t last_fetch_time;
    int total_requests;
    int successful_requests;
    int failed_requests;
    int total_records;
    int errors;
    int rate_limit_hits;
} stats_t;

/* Global state */
static volatile int g_running = 1;
static backfiller_config_t g_config = {0};
static stats_t g_stats = {0};
static pthread_mutex_t g_stats_mu = PTHREAD_MUTEX_INITIALIZER;

/* Buffer for curl response */
typedef struct {
    char *data;
    size_t size;
} buffer_t;

static size_t curl_response_write(void *contents, size_t size, size_t nmemb, void *userp)
{
    size_t realsize = size * nmemb;
    buffer_t *mem = (buffer_t *)userp;

    char *ptr = realloc(mem->data, mem->size + realsize + 1);
    if (!ptr) {
        return 0;
    }

    mem->data = ptr;
    memcpy(&(mem->data[mem->size]), contents, realsize);
    mem->size += realsize;
    mem->data[mem->size] = 0;

    return realsize;
}

/* Signal handler */
static void signal_handler(int sig)
{
    if (sig == SIGINT) {
        g_running = 0;
    }
}

/* Load configuration from JSON */
static int load_config(const char *config_file)
{
    FILE *f = fopen(config_file, "r");
    if (!f) {
        return -1;
    }

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    char *data = malloc(size + 1);
    if (!data) {
        fclose(f);
        return -1;
    }

    if (fread(data, 1, size, f) != (size_t)size) {
        free(data);
        fclose(f);
        return -1;
    }
    fclose(f);
    data[size] = 0;

    cJSON *root = cJSON_Parse(data);
    free(data);
    if (!root) {
        return -1;
    }

    cJSON *backfiller = cJSON_GetObjectItem(root, "smhi_backfiller");
    if (!backfiller) {
        cJSON_Delete(root);
        return -1;
    }

    cJSON *enabled = cJSON_GetObjectItem(backfiller, "enabled");
    g_config.enabled = enabled ? enabled->type == cJSON_True : 0;

    cJSON *locations = cJSON_GetObjectItem(backfiller, "locations");
    if (locations && locations->type == cJSON_Array) {
        int count = 0;
        cJSON *loc = locations->child;
        while (loc && count < 32) {
            cJSON *name = cJSON_GetObjectItem(loc, "name");
            cJSON *lat = cJSON_GetObjectItem(loc, "latitude");
            cJSON *lon = cJSON_GetObjectItem(loc, "longitude");

            if (name && lat && lon) {
                strncpy(g_config.locations[count].name, name->valuestring, 63);
                g_config.locations[count].latitude = lat->valuedouble;
                g_config.locations[count].longitude = lon->valuedouble;
                count++;
            }
            loc = loc->next;
        }
        g_config.num_locations = count;
    }

    cJSON *horizon = cJSON_GetObjectItem(backfiller, "forecast_horizon_hours");
    g_config.forecast_horizon_hours = horizon ? horizon->valueint : 72;

    cJSON *slot = cJSON_GetObjectItem(backfiller, "slot_interval_minutes");
    g_config.slot_interval_minutes = slot ? slot->valueint : 15;

    cJSON *fetch_int = cJSON_GetObjectItem(backfiller, "fetch_interval_seconds");
    g_config.fetch_interval_seconds = fetch_int ? fetch_int->valueint : 300;

    cJSON *delay = cJSON_GetObjectItem(backfiller, "request_delay_ms");
    g_config.request_delay_ms = delay ? delay->valueint : 1000;

    cJSON *batch = cJSON_GetObjectItem(backfiller, "batch_size");
    g_config.batch_size = batch ? batch->valueint : 5;

    cJSON *retry = cJSON_GetObjectItem(backfiller, "retry_on_error");
    g_config.retry_on_error = retry ? retry->type == cJSON_True : 1;

    cJSON *max_ret = cJSON_GetObjectItem(backfiller, "max_retries");
    g_config.max_retries = max_ret ? max_ret->valueint : 3;

    cJSON_Delete(root);
    return 0;
}

/* Fetch SMHI SNOW1G forecast data */
static int fetch_smhi_forecast(double latitude, double longitude, buffer_t *buffer)
{
    CURL *curl = curl_easy_init();
    if (!curl) {
        return -1;
    }

    char url[512];
    snprintf(url, sizeof(url),
        "https://opendata-download-metfcst.smhi.se/api/category/snow1g/version/1/geotype/point/lon/%.4f/lat/%.4f/data.json",
        longitude, latitude);

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_response_write);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)buffer);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);

    CURLcode res = curl_easy_perform(curl);

    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        fprintf(stderr, "  [ERROR] Curl failed: %s\n", curl_easy_strerror(res));
        return -1;
    }

    if (http_code == 429) {
        pthread_mutex_lock(&g_stats_mu);
        g_stats.rate_limit_hits++;
        pthread_mutex_unlock(&g_stats_mu);
        return -2; /* Rate limited */
    }

    if (http_code != 200) {
        return -1;
    }

    return 0;
}

/* Convert timestamp to 15-minute slot boundary */
static int64_t align_to_slot(int64_t ts, int slot_seconds)
{
    return ts - (ts % slot_seconds);
}

/* Write a single metric to database */
static int write_metric(ss_metric_id metric, double value, int64_t ts_start)
{
    ss_sdk_record rec = {
        .metric = metric,
        .value_type = SS_SDK_VALUE_F64,
        .value.f64 = value,
        .ts_start_utc = ts_start,
        .ts_end_utc = ts_start + 900,
        .data_kind = SS_SDK_DATA_FORECAST
    };

    ss_sdk_status status = ss_sdk_db_write_record(&rec);
    if (status != SS_SDK_OK) {
        return -1;
    }

    return 0;
}

/* Process SMHI forecast JSON response */
static int process_smhi_forecast(const char *json_str, const char *location_name)
{
    cJSON *root = cJSON_Parse(json_str);
    if (!root) {
        return -1;
    }

    cJSON *timeseries = cJSON_GetObjectItem(root, "timeSeries");
    if (!timeseries || timeseries->type != cJSON_Array) {
        cJSON_Delete(root);
        return -1;
    }

    int records_written = 0;
    int slot_seconds = g_config.slot_interval_minutes * 60;

    cJSON *ts = timeseries->child;
    while (ts) {
        cJSON *time_obj = cJSON_GetObjectItem(ts, "validTime");
        cJSON *params = cJSON_GetObjectItem(ts, "parameters");

        if (time_obj && params) {
            /* Parse ISO 8601 timestamp */
            struct tm tm = {0};
            sscanf(time_obj->valuestring, "%d-%d-%dT%d:%d:%dZ",
                &tm.tm_year, &tm.tm_mon, &tm.tm_mday,
                &tm.tm_hour, &tm.tm_min, &tm.tm_sec);
            tm.tm_year -= 1900;
            tm.tm_mon -= 1;

            time_t t = mktime(&tm);
            int64_t slot_utc = align_to_slot(t, slot_seconds);

            /* Extract parameters and write to database */
            cJSON *param = params->child;
            while (param) {
                const char *name = param->string;
                cJSON *values = cJSON_GetObjectItem(param, "values");

                if (name && values && values->type == cJSON_Array && values->child) {
                    double value = values->child->valuedouble;

                    /* Map SMHI parameters to canonical metrics */
                    if (strcmp(name, "t") == 0) {
                        /* Temperature */
                        write_metric(SS_METRIC_WEATHER_TEMPERATURE_AIR_2M_C, value, slot_utc);
                        records_written++;
                    } else if (strcmp(name, "r") == 0) {
                        /* Relative humidity */
                        write_metric(SS_METRIC_WEATHER_HUMIDITY_RELATIVE_2M_PCT, value, slot_utc);
                        records_written++;
                    } else if (strcmp(name, "ws") == 0) {
                        /* Wind speed */
                        write_metric(SS_METRIC_WEATHER_WIND_SPEED_10M_MS, value, slot_utc);
                        records_written++;
                    } else if (strcmp(name, "wd") == 0) {
                        /* Wind direction */
                        write_metric(SS_METRIC_WEATHER_WIND_DIRECTION_10M_DEG, value, slot_utc);
                        records_written++;
                    } else if (strcmp(name, "p") == 0) {
                        /* Pressure */
                        write_metric(SS_METRIC_WEATHER_PRESSURE_MSL_HPA, value, slot_utc);
                        records_written++;
                    }
                }
                param = param->next;
            }
        }
        ts = ts->next;
    }

    cJSON_Delete(root);
    return records_written;
}

/* Update console UI */
static void update_ui(void)
{
    /* Clear screen and print header */
    printf("\033[H\033[2J"); /* Clear screen */
    printf("\n");
    printf("╔════════════════════════════════════════════════════════════════════════════════╗\n");
    printf("║                   SMHI Weather Data Backfiller                                  ║\n");
    printf("╚════════════════════════════════════════════════════════════════════════════════╝\n\n");

    pthread_mutex_lock(&g_stats_mu);

    int64_t now = time(NULL);
    int uptime = now - g_stats.start_time;

    printf("Status:              Running\n");
    printf("Uptime:              %d seconds (%d min)\n", uptime, uptime / 60);
    printf("Locations:           %d\n\n", g_config.num_locations);

    printf("Requests:            %d total | %d success | %d failed\n",
        g_stats.total_requests, g_stats.successful_requests, g_stats.failed_requests);
    printf("Records Written:     %d\n", g_stats.total_records);
    printf("Rate Limit Hits:     %d\n", g_stats.rate_limit_hits);
    printf("Errors:              %d\n\n", g_stats.errors);

    if (g_stats.last_fetch_time > 0) {
        int since_fetch = now - g_stats.last_fetch_time;
        printf("Last Fetch:          %d seconds ago\n", since_fetch);
    }

    printf("Config:\n");
    printf("  Forecast Horizon:  %d hours\n", g_config.forecast_horizon_hours);
    printf("  Slot Interval:     %d minutes\n", g_config.slot_interval_minutes);
    printf("  Fetch Interval:    %d seconds\n", g_config.fetch_interval_seconds);
    printf("  Request Delay:     %d ms\n", g_config.request_delay_ms);

    printf("\nLocations:\n");
    for (int i = 0; i < g_config.num_locations && i < 5; i++) {
        printf("  • %s (%.4f°, %.4f°)\n",
            g_config.locations[i].name,
            g_config.locations[i].latitude,
            g_config.locations[i].longitude);
    }

    printf("\n");
    printf("Press Ctrl+C to stop gracefully...\n\n");

    pthread_mutex_unlock(&g_stats_mu);
    fflush(stdout);
}

/* Main fetch loop */
static int run_backfiller(void)
{
    g_stats.start_time = time(NULL);

    fprintf(stderr, "[SMHI Backfiller] Starting with %d location(s)\n", g_config.num_locations);

    while (g_running) {
        update_ui();

        for (int loc = 0; loc < g_config.num_locations && g_running; loc++) {
            location_t *loc_info = &g_config.locations[loc];

            buffer_t buffer = {0};
            int fetch_result = fetch_smhi_forecast(loc_info->latitude, loc_info->longitude, &buffer);

            pthread_mutex_lock(&g_stats_mu);
            g_stats.total_requests++;

            if (fetch_result == -2) {
                /* Rate limited, back off */
                pthread_mutex_unlock(&g_stats_mu);
                fprintf(stderr, "[SMHI Backfiller] Rate limited at %s, backing off 60 seconds\n", loc_info->name);
                sleep(60);
                continue;
            } else if (fetch_result != 0) {
                g_stats.failed_requests++;
                g_stats.errors++;
                pthread_mutex_unlock(&g_stats_mu);
                free(buffer.data);
                sleep(5);
                continue;
            }

            int records = process_smhi_forecast(buffer.data, loc_info->name);
            if (records > 0) {
                g_stats.successful_requests++;
                g_stats.total_records += records;
            } else {
                g_stats.failed_requests++;
                g_stats.errors++;
            }

            g_stats.last_fetch_time = time(NULL);
            pthread_mutex_unlock(&g_stats_mu);

            free(buffer.data);

            if (loc < g_config.num_locations - 1) {
                struct timespec ts = {
                    .tv_sec = 0,
                    .tv_nsec = (long)g_config.request_delay_ms * 1000000L
                };
                nanosleep(&ts, NULL);
            }
        }

        sleep(g_config.fetch_interval_seconds);
    }

    return 0;
}

int main(int argc, char *argv[])
{
    const char *config_file = (argc > 1) ? argv[1] : "config/sunspots.json";
    const char *db_path = (argc > 2) ? argv[2] : "db/smhi_forecast.db";
    
    /* Set database path */
    if (setenv("SS_SDK_DB_PATH", db_path, 1) != 0) {
        fprintf(stderr, "Failed to set database path\n");
        return 1;
    }

    /* Allocate location array */
    g_config.locations = malloc(32 * sizeof(location_t));
    if (!g_config.locations) {
        fprintf(stderr, "Failed to allocate memory\n");
        return 1;
    }

    if (load_config(config_file) != 0) {
        fprintf(stderr, "Failed to load config from %s\n", config_file);
        free(g_config.locations);
        return 1;
    }

    if (!g_config.enabled || g_config.num_locations <= 0) {
        fprintf(stderr, "SMHI backfiller not enabled or no locations configured\n");
        free(g_config.locations);
        return 1;
    }

    /* Handle Ctrl+C gracefully */
    signal(SIGINT, signal_handler);

    fprintf(stderr, "[SMHI Backfiller] Loaded config: %d locations\n", g_config.num_locations);
    fprintf(stderr, "[SMHI Backfiller] Forecast horizon: %d hours\n", g_config.forecast_horizon_hours);

    int result = run_backfiller();

    fprintf(stderr, "\n[SMHI Backfiller] Shutting down gracefully...\n");
    fprintf(stderr, "[SMHI Backfiller] Total records written: %d\n", g_stats.total_records);
    fprintf(stderr, "[SMHI Backfiller] Total requests: %d (success: %d, failed: %d)\n",
        g_stats.total_requests, g_stats.successful_requests, g_stats.failed_requests);

    ss_sdk_shutdown();
    free(g_config.locations);

    return result;
}
