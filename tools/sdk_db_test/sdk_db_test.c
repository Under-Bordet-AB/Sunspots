#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <curl/curl.h>
#include <cJSON.h>
#include <math.h>

#include "sdk/ss_sdk.h"

/* Memory buffer for curl response */
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
        fprintf(stderr, "Not enough memory for curl response\n");
        return 0;
    }

    mem->data = ptr;
    memcpy(&(mem->data[mem->size]), contents, realsize);
    mem->size += realsize;
    mem->data[mem->size] = 0;

    return realsize;
}

/* Fetch weather data from Open-Meteo API */
static int fetch_openmeteo_data(const char *latitude, const char *longitude, buffer_t *buffer)
{
    CURL *curl;
    CURLcode res;
    char url[512];

    curl = curl_easy_init();
    if (!curl) {
        fprintf(stderr, "Failed to init curl\n");
        return -1;
    }

    snprintf(url, sizeof(url),
        "https://api.open-meteo.com/v1/forecast?"
        "latitude=%s&longitude=%s&current=temperature_2m,relative_humidity_2m,"
        "weather_code,wind_speed_10m,wind_direction_10m&timezone=UTC",
        latitude, longitude);

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_response_write);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)buffer);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);

    fprintf(stderr, "Fetching from: %s\n", url);
    res = curl_easy_perform(curl);

    if (res != CURLE_OK) {
        fprintf(stderr, "Curl error: %s\n", curl_easy_strerror(res));
        curl_easy_cleanup(curl);
        return -1;
    }

    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    curl_easy_cleanup(curl);

    if (http_code != 200) {
        fprintf(stderr, "HTTP error: %ld\n", http_code);
        return -1;
    }

    return 0;
}

/* Convert timestamp to 15-minute slot boundary (UTC) */
static int64_t align_to_slot(time_t t)
{
    int64_t ts = (int64_t)t;
    return ts - (ts % 900);
}

/* Write a canonical record to the database */
static int write_record(ss_metric_id metric, ss_sdk_value_type value_type, ss_sdk_value value)
{
    int64_t now_slot = align_to_slot(time(NULL));
    
    ss_sdk_record rec = {
        .metric = metric,
        .value_type = value_type,
        .value = value,
        .ts_start_utc = now_slot,
        .ts_end_utc = now_slot + 900,
        .data_kind = SS_SDK_DATA_OBSERVATION
    };

    ss_sdk_status status = ss_sdk_db_write_record(&rec);
    if (status != SS_SDK_OK) {
        fprintf(stderr, "Failed to write record for metric %d: %d\n", metric, status);
        return -1;
    }

    fprintf(stderr, "Wrote record: metric=%d, ts=%ld\n", metric, now_slot);
    return 0;
}

/* Write OpenMeteo weather data to database */
static int process_openmeteo_data(const char *json_str)
{
    cJSON *root = cJSON_Parse(json_str);
    if (!root) {
        fprintf(stderr, "Failed to parse JSON response\n");
        return -1;
    }

    cJSON *current = cJSON_GetObjectItem(root, "current");
    if (!current) {
        fprintf(stderr, "No 'current' object in response\n");
        cJSON_Delete(root);
        return -1;
    }

    int count = 0;

    /* Temperature (C) */
    cJSON *temp = cJSON_GetObjectItem(current, "temperature_2m");
    if (temp && temp->type == cJSON_Number) {
        ss_sdk_value val = {.f64 = temp->valuedouble};
        if (write_record(SS_METRIC_WEATHER_TEMPERATURE_AIR_2M_C, SS_SDK_VALUE_F64, val) == 0) {
            count++;
        }
    }

    /* Humidity (%) */
    cJSON *humidity = cJSON_GetObjectItem(current, "relative_humidity_2m");
    if (humidity && humidity->type == cJSON_Number) {
        ss_sdk_value val = {.f64 = humidity->valuedouble};
        if (write_record(SS_METRIC_WEATHER_HUMIDITY_RELATIVE_2M_PCT, SS_SDK_VALUE_F64, val) == 0) {
            count++;
        }
    }

    /* Wind Speed (m/s) */
    cJSON *wind_speed = cJSON_GetObjectItem(current, "wind_speed_10m");
    if (wind_speed && wind_speed->type == cJSON_Number) {
        ss_sdk_value val = {.f64 = wind_speed->valuedouble};
        if (write_record(SS_METRIC_WEATHER_WIND_SPEED_10M_MS, SS_SDK_VALUE_F64, val) == 0) {
            count++;
        }
    }

    /* Wind Direction (deg) */
    cJSON *wind_dir = cJSON_GetObjectItem(current, "wind_direction_10m");
    if (wind_dir && wind_dir->type == cJSON_Number) {
        ss_sdk_value val = {.f64 = wind_dir->valuedouble};
        if (write_record(SS_METRIC_WEATHER_WIND_DIRECTION_10M_DEG, SS_SDK_VALUE_F64, val) == 0) {
            count++;
        }
    }

    /* Weather Code (as integer) */
    cJSON *weather_code = cJSON_GetObjectItem(current, "weather_code");
    if (weather_code && weather_code->type == cJSON_Number) {
        ss_sdk_value val = {.i64 = (int64_t)weather_code->valuedouble};
        if (write_record(SS_METRIC_WEATHER_CONDITION_SYMBOL_CODE, SS_SDK_VALUE_I64, val) == 0) {
            count++;
        }
    }

    fprintf(stderr, "Processed %d metrics\n", count);

    cJSON_Delete(root);
    return (count > 0) ? 0 : -1;
}

/* Read records from database and output as JSON */
static int read_and_output_json(const char *output_file, int quarters_to_read)
{
    FILE *out = fopen(output_file, "w");
    if (!out) {
        fprintf(stderr, "Failed to open output file: %s\n", output_file);
        return -1;
    }

    /* Get current slot time */
    int64_t now_slot = align_to_slot(time(NULL));
    
    cJSON *root = cJSON_CreateObject();
    cJSON *records = cJSON_CreateArray();
    cJSON_AddItemToObject(root, "records", records);
    cJSON_AddNumberToObject(root, "fetch_timestamp", (double)time(NULL));
    cJSON_AddNumberToObject(root, "start_utc", (double)now_slot);

    /* Metrics to read */
    ss_metric_id metrics[] = {
        SS_METRIC_WEATHER_TEMPERATURE_AIR_2M_C,
        SS_METRIC_WEATHER_HUMIDITY_RELATIVE_2M_PCT,
        SS_METRIC_WEATHER_WIND_SPEED_10M_MS,
        SS_METRIC_WEATHER_WIND_DIRECTION_10M_DEG,
        SS_METRIC_WEATHER_CONDITION_SYMBOL_CODE
    };
    int num_metrics = sizeof(metrics) / sizeof(metrics[0]);

    /* Read each metric */
    for (int i = 0; i < num_metrics; i++) {
        ss_sdk_samples_out samples = {0};
        ss_sdk_status status = ss_sdk_db_get_canonical(now_slot, quarters_to_read, metrics[i], &samples);

        if (status == SS_SDK_OK && samples.count > 0) {
            fprintf(stderr, "Read %zu samples for metric %d\n", samples.count, metrics[i]);

            for (size_t j = 0; j < samples.count; j++) {
                cJSON *sample = cJSON_CreateObject();
                cJSON_AddNumberToObject(sample, "ts_utc", (double)samples.samples[j].ts_utc);
                cJSON_AddStringToObject(sample, "metric_id", ss_metric_name(samples.samples[j].canonical));
                cJSON_AddStringToObject(sample, "value_type", 
                    (samples.samples[j].value_type == SS_SDK_VALUE_F64) ? "f64" :
                    (samples.samples[j].value_type == SS_SDK_VALUE_I64) ? "i64" : "bool");

                if (samples.samples[j].value_type == SS_SDK_VALUE_F64) {
                    cJSON_AddNumberToObject(sample, "value", samples.samples[j].value.f64);
                } else if (samples.samples[j].value_type == SS_SDK_VALUE_I64) {
                    cJSON_AddNumberToObject(sample, "value", (double)samples.samples[j].value.i64);
                } else {
                    cJSON_AddBoolToObject(sample, "value", samples.samples[j].value.boolean);
                }

                cJSON_AddStringToObject(sample, "flags",
                    (samples.samples[j].flags & SS_SDK_SAMPLE_OBSERVED) ? "observed" :
                    (samples.samples[j].flags & SS_SDK_SAMPLE_INTERPOLATED) ? "interpolated" : "forecast");

                cJSON_AddItemToArray(records, sample);
            }

            ss_sdk_db_free_samples(&samples);
        } else {
            fprintf(stderr, "No samples read for metric %d (status=%d)\n", metrics[i], status);
        }
    }

    char *json_str = cJSON_Print(root);
    if (json_str) {
        fprintf(out, "%s\n", json_str);
        free(json_str);
    }

    cJSON_Delete(root);
    fclose(out);

    fprintf(stderr, "JSON output written to: %s\n", output_file);
    return 0;
}

int main(int argc, char *argv[])
{
    const char *latitude = (argc > 1) ? argv[1] : "59.3293";   /* Stockholm */
    const char *longitude = (argc > 2) ? argv[2] : "18.0686";  /* Stockholm */
    const char *output_file = (argc > 3) ? argv[3] : "sdk_output.json";
    const char *db_path = (argc > 4) ? argv[4] : "db/sdk_test.db";

    /* Set custom database path */
    if (setenv("SS_SDK_DB_PATH", db_path, 1) != 0) {
        fprintf(stderr, "Failed to set SS_SDK_DB_PATH\n");
        return 1;
    }

    fprintf(stderr, "=== Sunspots SDK Database Test ===\n");
    fprintf(stderr, "Location: %s, %s\n", latitude, longitude);
    fprintf(stderr, "Database: %s\n", db_path);
    fprintf(stderr, "Output: %s\n\n", output_file);

    /* Fetch OpenMeteo data */
    fprintf(stderr, "[1] Fetching OpenMeteo data...\n");
    buffer_t buffer = {0};
    if (fetch_openmeteo_data(latitude, longitude, &buffer) < 0) {
        fprintf(stderr, "Failed to fetch OpenMeteo data\n");
        free(buffer.data);
        return 1;
    }

    fprintf(stderr, "Received %zu bytes\n\n", buffer.size);

    /* Process and write to database */
    fprintf(stderr, "[2] Writing records to SDK database...\n");
    if (process_openmeteo_data(buffer.data) < 0) {
        fprintf(stderr, "Failed to process OpenMeteo data\n");
        free(buffer.data);
        return 1;
    }

    free(buffer.data);

    fprintf(stderr, "\n[3] Reading from database and generating JSON...\n");
    if (read_and_output_json(output_file, 1) < 0) {
        fprintf(stderr, "Failed to read from database\n");
        return 1;
    }

    fprintf(stderr, "\n=== Test Complete ===\n");
    fprintf(stderr, "Check %s to verify database contents\n", output_file);

    ss_sdk_shutdown();
    return 0;
}
