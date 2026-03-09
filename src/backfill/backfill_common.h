#ifndef SUNSPOTS_BACKFILL_COMMON_H
#define SUNSPOTS_BACKFILL_COMMON_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "../libs/json/cJSON.h"

#define BACKFILL_SLOT_SECONDS 900
#define BACKFILL_MAX_URL_LEN 1024
#define BACKFILL_OPENMETEO_LIMIT_PER_MIN 600
#define BACKFILL_OPENMETEO_LIMIT_PER_HOUR 5000
#define BACKFILL_OPENMETEO_LIMIT_PER_DAY 10000
#define BACKFILL_FORECAST_RUN_STEP_SECONDS 3600
#define BACKFILL_FORECAST_DAYS_PER_RUN 16
#define BACKFILL_SINGLE_RUNS_ENDPOINT "https://single-runs-api.open-meteo.com/v1/forecast"
#define BACKFILL_USAGE_DAILY_PATH "logs/backfill_usage_daily.log"
#define BACKFILL_USAGE_KEEP_DAYS 34

typedef struct {
    int enabled;
    int chunk_days;
    int retry_max_attempts;
    int retry_base_backoff_ms;
    int progress_log_interval_sec;
    int freshness_lag_minutes;
    int request_interval_ms;
    int max_requests_per_minute;
    int max_requests_per_hour;
    int max_requests_per_day;
    double latitude;
    double longitude;
    int has_system_location;
    char location_name[64];
    char elprisomrade[16];
    char endpoint[BACKFILL_MAX_URL_LEN];
    char single_runs_endpoint[BACKFILL_MAX_URL_LEN];
    char usage_daily_path[BACKFILL_MAX_URL_LEN];
    char start_date_utc[11];
    int64_t start_date_epoch_utc;
} backfill_config;

typedef struct {
    int64_t from_utc;
    int64_t to_utc;
} hole_range;

typedef struct {
    hole_range *items;
    size_t count;
    size_t cap;
} hole_list;

typedef struct {
    cJSON *times;
    cJSON *temp;
    cJSON *cloud;
    cJSON *rad;
} archive_hourly_arrays;

int64_t backfill_align_to_slot(int64_t ts_utc);
void backfill_copy_string_safe(char *dst, size_t dst_sz, const char *src);
int backfill_parse_utc_hour(const char *s, int64_t *out_epoch);
int backfill_parse_utc_date(const char *s, int64_t *out_epoch);
int backfill_epoch_to_ymd_utc(int64_t ts_utc, char out[11]);
int backfill_epoch_to_ymdhm_utc(int64_t ts_utc, char out[17]);

#endif
