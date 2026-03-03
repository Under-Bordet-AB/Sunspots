#define _GNU_SOURCE
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "../fetch/fetch_utils.h"
#include "../libs/json/cJSON.h"
#include "../sdk/ss_sdk.h"
#include "../sdk/internal/ss_sdk_config_util.h"

#define SLOT_SECONDS 900
#define MAX_READ_SLOTS 672
#define MAX_URL_LEN 1024
#define OPENMETEO_LIMIT_PER_MIN 600
#define OPENMETEO_LIMIT_PER_HOUR 5000
#define OPENMETEO_LIMIT_PER_DAY 10000

typedef struct {
    int enabled;
    int required_history_days;
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
    char endpoint[MAX_URL_LEN];
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
    int minute_epoch;
    int hour_epoch;
    int day_epoch;
    int minute_count;
    int hour_count;
    int day_count;
} rate_limiter;

typedef struct {
    cJSON *times;
    cJSON *temp;
    cJSON *cloud;
    cJSON *rad;
} archive_hourly_arrays;

typedef struct {
    int64_t win_start;
    int64_t win_end;
    uint16_t quarters;
    const ss_sdk_samples_out *samples;
    bool **out_present;
} present_mark_ctx;

typedef struct {
    const char *key;
    int min_v;
    int *field;
} cfg_int_binding;

static int64_t now_utc(void)
{
    return (int64_t)time(NULL);
}

static int64_t align_to_slot(int64_t ts_utc)
{
    if (ts_utc < 0) {
        return 0;
    }
    return ts_utc - (ts_utc % SLOT_SECONDS);
}

static int clamp_min_int(int v, int min_v, int fallback)
{
    if (v < min_v) {
        return fallback;
    }
    return v;
}

static void copy_string_safe(char *dst, size_t dst_sz, const char *src)
{
    int n;
    if (dst == NULL || dst_sz == 0U) {
        return;
    }
    if (src == NULL) {
        dst[0] = '\0';
        return;
    }
    n = snprintf(dst, dst_sz, "%s", src);
    if (n < 0) {
        dst[0] = '\0';
        return;
    }
    if ((size_t)n >= dst_sz) {
        dst[dst_sz - 1U] = '\0';
    }
}

static void sleep_ms(int ms)
{
    struct timespec req;
    if (ms <= 0) {
        return;
    }
    req.tv_sec = ms / 1000;
    req.tv_nsec = (long)(ms % 1000) * 1000000L;
    nanosleep(&req, NULL);
}

static bool hole_list_push_merge(hole_list *holes, int64_t from_utc, int64_t to_utc)
{
    hole_range *grown;
    if (holes == NULL || from_utc >= to_utc) {
        return false;
    }

    if (holes->count > 0) {
        hole_range *last = &holes->items[holes->count - 1U];
        if (from_utc <= last->to_utc) {
            if (to_utc > last->to_utc) {
                last->to_utc = to_utc;
            }
            return true;
        }
    }

    if (holes->count == holes->cap) {
        size_t new_cap = holes->cap == 0U ? 16U : holes->cap * 2U;
        grown = (hole_range *)realloc(holes->items, new_cap * sizeof(*grown));
        if (grown == NULL) {
            return false;
        }
        holes->items = grown;
        holes->cap = new_cap;
    }

    holes->items[holes->count].from_utc = from_utc;
    holes->items[holes->count].to_utc = to_utc;
    holes->count += 1U;
    return true;
}

static void hole_list_free(hole_list *holes)
{
    if (holes == NULL) {
        return;
    }
    free(holes->items);
    holes->items = NULL;
    holes->count = 0U;
    holes->cap = 0U;
}

static int parse_utc_hour(const char *s, int64_t *out_epoch)
{
    struct tm tmv;
    char *endp;
    time_t epoch;

    if (s == NULL || out_epoch == NULL) {
        return -1;
    }

    memset(&tmv, 0, sizeof(tmv));
    endp = strptime(s, "%Y-%m-%dT%H:%M", &tmv);
    if (endp == NULL || *endp != '\0') {
        return -1;
    }

    epoch = timegm(&tmv);
    if (epoch < 0) {
        return -1;
    }
    *out_epoch = (int64_t)epoch;
    return 0;
}

static int epoch_to_ymd_utc(int64_t ts_utc, char out[11])
{
    time_t tv;
    struct tm tmv;
    if (out == NULL) {
        return -1;
    }
    tv = (time_t)ts_utc;
    if (gmtime_r(&tv, &tmv) == NULL) {
        return -1;
    }
    if (strftime(out, 11, "%Y-%m-%d", &tmv) != 10U) {
        return -1;
    }
    return 0;
}

static void backfill_config_defaults(backfill_config *cfg)
{
    memset(cfg, 0, sizeof(*cfg));
    cfg->enabled = 1;
    cfg->required_history_days = 14;
    cfg->chunk_days = 7;
    cfg->retry_max_attempts = 5;
    cfg->retry_base_backoff_ms = 1000;
    cfg->progress_log_interval_sec = 10;
    cfg->freshness_lag_minutes = 120;
    cfg->request_interval_ms = 250;
    cfg->max_requests_per_minute = 240;
    cfg->max_requests_per_hour = 2000;
    cfg->max_requests_per_day = 8000;
    cfg->latitude = 0.0;
    cfg->longitude = 0.0;
    cfg->has_system_location = 0;
    cfg->location_name[0] = '\0';
    cfg->elprisomrade[0] = '\0';
    copy_string_safe(cfg->endpoint, sizeof(cfg->endpoint), "https://archive-api.open-meteo.com/v1/archive");
}

static void backfill_config_apply_system_location(backfill_config *cfg)
{
    ss_sdk_cfg_location location;
    ss_sdk_cfg_status status;

    if (cfg == NULL) {
        return;
    }

    status = ss_sdk_cfg_get_location_from_system_env(&location);
    if (status != SS_SDK_CFG_OK) {
        return;
    }

    cfg->latitude = location.latitude;
    cfg->longitude = location.longitude;
    cfg->has_system_location = 1;
    if (location.name[0] != '\0') {
        copy_string_safe(cfg->location_name, sizeof(cfg->location_name), location.name);
    }
    if (location.elprisomrade[0] != '\0') {
        copy_string_safe(cfg->elprisomrade, sizeof(cfg->elprisomrade), location.elprisomrade);
    }
}

static int backfill_cfg_path(char *out, size_t out_sz, const char *key, int with_backfill_prefix)
{
    int n;
    if (out == NULL || out_sz == 0U || key == NULL || key[0] == '\0') {
        return -1;
    }
    if (with_backfill_prefix) {
        n = snprintf(out, out_sz, "backfill.%s", key);
    } else {
        n = snprintf(out, out_sz, "%s", key);
    }
    if (n <= 0 || (size_t)n >= out_sz) {
        return -1;
    }
    return 0;
}

static int backfill_cfg_try_get_bool(const char *key, int *out_bool)
{
    char path[64];
    if (out_bool == NULL || key == NULL) {
        return 0;
    }

    if (backfill_cfg_path(path, sizeof(path), key, 1) == 0 &&
        ss_sdk_cfg_get_bool_from_env_json("SUNSPOTS_CONFIG", path, out_bool) == SS_SDK_CFG_OK) {
        return 1;
    }
    if (backfill_cfg_path(path, sizeof(path), key, 0) == 0 &&
        ss_sdk_cfg_get_bool_from_env_json("SUNSPOTS_CONFIG", path, out_bool) == SS_SDK_CFG_OK) {
        return 1;
    }
    return 0;
}

static int backfill_cfg_try_get_int(const char *key, int *out_int)
{
    char path[64];
    if (out_int == NULL || key == NULL) {
        return 0;
    }

    if (backfill_cfg_path(path, sizeof(path), key, 1) == 0 &&
        ss_sdk_cfg_get_int_from_env_json("SUNSPOTS_CONFIG", path, INT_MIN, INT_MAX, out_int) == SS_SDK_CFG_OK) {
        return 1;
    }
    if (backfill_cfg_path(path, sizeof(path), key, 0) == 0 &&
        ss_sdk_cfg_get_int_from_env_json("SUNSPOTS_CONFIG", path, INT_MIN, INT_MAX, out_int) == SS_SDK_CFG_OK) {
        return 1;
    }
    return 0;
}

static int backfill_cfg_try_get_string(const char *key, char *out, size_t out_sz)
{
    char path[64];
    if (out == NULL || out_sz == 0U || key == NULL) {
        return 0;
    }

    if (backfill_cfg_path(path, sizeof(path), key, 1) == 0 &&
        ss_sdk_cfg_get_string_from_env_json("SUNSPOTS_CONFIG", path, out, out_sz) == SS_SDK_CFG_OK) {
        return 1;
    }
    if (backfill_cfg_path(path, sizeof(path), key, 0) == 0 &&
        ss_sdk_cfg_get_string_from_env_json("SUNSPOTS_CONFIG", path, out, out_sz) == SS_SDK_CFG_OK) {
        return 1;
    }
    return 0;
}

static void backfill_cfg_apply_int(const char *key, int min_v, int *field)
{
    int value = 0;
    if (key == NULL || field == NULL) {
        return;
    }
    if (backfill_cfg_try_get_int(key, &value)) {
        *field = clamp_min_int(value, min_v, *field);
    }
}

static void backfill_config_parse(backfill_config *cfg)
{
    int value = 0;
    cfg_int_binding int_bindings[] = {
        {"required_history_days", 1, &cfg->required_history_days},
        {"chunk_days", 1, &cfg->chunk_days},
        {"retry_max_attempts", 1, &cfg->retry_max_attempts},
        {"retry_base_backoff_ms", 100, &cfg->retry_base_backoff_ms},
        {"progress_log_interval_sec", 1, &cfg->progress_log_interval_sec},
        {"freshness_lag_minutes", 0, &cfg->freshness_lag_minutes},
        {"request_interval_ms", 50, &cfg->request_interval_ms},
        {"max_requests_per_minute", 1, &cfg->max_requests_per_minute},
        {"max_requests_per_hour", 1, &cfg->max_requests_per_hour},
        {"max_requests_per_day", 1, &cfg->max_requests_per_day},
    };
    size_t i;

    backfill_config_defaults(cfg);

    if (backfill_cfg_try_get_bool("enabled", &value)) {
        cfg->enabled = value ? 1 : 0;
    }

    for (i = 0U; i < sizeof(int_bindings) / sizeof(int_bindings[0]); ++i) {
        backfill_cfg_apply_int(int_bindings[i].key, int_bindings[i].min_v, int_bindings[i].field);
    }
    if (cfg->max_requests_per_minute > OPENMETEO_LIMIT_PER_MIN) {
        cfg->max_requests_per_minute = OPENMETEO_LIMIT_PER_MIN;
    }

    if (cfg->max_requests_per_hour > OPENMETEO_LIMIT_PER_HOUR) {
        cfg->max_requests_per_hour = OPENMETEO_LIMIT_PER_HOUR;
    }

    if (cfg->max_requests_per_day > OPENMETEO_LIMIT_PER_DAY) {
        cfg->max_requests_per_day = OPENMETEO_LIMIT_PER_DAY;
    }

    (void)backfill_cfg_try_get_string("endpoint", cfg->endpoint, sizeof(cfg->endpoint));
    backfill_config_apply_system_location(cfg);
}

static int archive_extract_arrays(cJSON *root, archive_hourly_arrays *arrays)
{
    cJSON *hourly = NULL;
    if (root == NULL || arrays == NULL) {
        return -1;
    }

    memset(arrays, 0, sizeof(*arrays));
    hourly = cJSON_GetObjectItemCaseSensitive(root, "hourly");
    if (!cJSON_IsObject(hourly)) {
        return -1;
    }

    arrays->times = cJSON_GetObjectItemCaseSensitive(hourly, "time");
    arrays->temp = cJSON_GetObjectItemCaseSensitive(hourly, "temperature_2m");
    arrays->cloud = cJSON_GetObjectItemCaseSensitive(hourly, "cloud_cover");
    arrays->rad = cJSON_GetObjectItemCaseSensitive(hourly, "shortwave_radiation");
    if (!cJSON_IsArray(arrays->times)) {
        return -1;
    }

    return 0;
}

static void log_cfg(const backfill_config *cfg)
{
    char msg[384];
    if (snprintf(msg,
                 sizeof(msg),
                 "enabled=%d location=%s area=%s history_days=%d chunk_days=%d retries=%d backoff_ms=%d lag_min=%d req_interval_ms=%d limit_m=%d limit_h=%d limit_d=%d lat=%.4f lon=%.4f endpoint=%.80s",
                 cfg->enabled,
                 cfg->location_name,
                 cfg->elprisomrade,
                 cfg->required_history_days,
                 cfg->chunk_days,
                 cfg->retry_max_attempts,
                 cfg->retry_base_backoff_ms,
                 cfg->freshness_lag_minutes,
                 cfg->request_interval_ms,
                 cfg->max_requests_per_minute,
                 cfg->max_requests_per_hour,
                 cfg->max_requests_per_day,
                 cfg->latitude,
                 cfg->longitude,
                 cfg->endpoint) < 0) {
        copy_string_safe(msg, sizeof(msg), "config logging failed");
    }
    (void)SS_LOG_INFO("backfill.config", msg);
}

static int floor_div_pos(int64_t num, int denom)
{
    if (denom <= 0) {
        return 0;
    }
    if (num < 0) {
        return 0;
    }
    return (int)(num / denom);
}

static int compute_wait_seconds(int64_t now_utc, bool hit_minute, bool hit_hour, bool hit_day)
{
    int sec_to_minute = 0;
    int sec_to_hour = 0;
    int sec_to_day = 0;
    int wait_sec = 0;

    sec_to_minute = 60 - (int)(now_utc % 60);
    sec_to_hour = 3600 - (int)(now_utc % 3600);
    sec_to_day = 86400 - (int)(now_utc % 86400);

    if (hit_minute) {
        wait_sec = sec_to_minute;
    }
    if (hit_hour && (wait_sec == 0 || sec_to_hour > wait_sec)) {
        wait_sec = sec_to_hour;
    }
    if (hit_day && (wait_sec == 0 || sec_to_day > wait_sec)) {
        wait_sec = sec_to_day;
    }
    return wait_sec;
}

static void rate_limiter_maybe_wait(rate_limiter *rl, const backfill_config *cfg)
{
    for (;;) {
        int64_t nowv = now_utc();
        int minute_epoch = floor_div_pos(nowv, 60);
        int hour_epoch = floor_div_pos(nowv, 3600);
        int day_epoch = floor_div_pos(nowv, 86400);
        int wait_sec;
        bool hit_minute;
        bool hit_hour;
        bool hit_day;
        char msg[128];

        if (rl->minute_epoch != minute_epoch) {
            rl->minute_epoch = minute_epoch;
            rl->minute_count = 0;
        }
        if (rl->hour_epoch != hour_epoch) {
            rl->hour_epoch = hour_epoch;
            rl->hour_count = 0;
        }
        if (rl->day_epoch != day_epoch) {
            rl->day_epoch = day_epoch;
            rl->day_count = 0;
        }

        hit_minute = (rl->minute_count >= cfg->max_requests_per_minute);
        hit_hour = (rl->hour_count >= cfg->max_requests_per_hour);
        hit_day = (rl->day_count >= cfg->max_requests_per_day);

        if (!hit_minute && !hit_hour && !hit_day) {
            rl->minute_count += 1;
            rl->hour_count += 1;
            rl->day_count += 1;
            return;
        }

        wait_sec = compute_wait_seconds(nowv, hit_minute, hit_hour, hit_day);
        if (snprintf(msg, sizeof(msg), "throttling wait=%ds", wait_sec) < 0) {
            copy_string_safe(msg, sizeof(msg), "throttling wait");
        }
        (void)SS_LOG_WARN("backfill.rate_limit_wait", msg);
        sleep_ms(wait_sec * 1000);
    }
}

static int fetch_json_with_retry(const char *url, int max_attempts, int base_backoff_ms, char **out_buf)
{
    int attempt;
    char msg[256];
    int rc = -1;

    if (url == NULL || out_buf == NULL) {
        return -1;
    }
    *out_buf = NULL;

    for (attempt = 1; attempt <= max_attempts; ++attempt) {
        char *buf = NULL;
        if (fetch_from_url((char *)url, &buf, 30) == 0 && buf != NULL) {
            *out_buf = buf;
            return 0;
        }
        free(buf);
        int wait_ms = base_backoff_ms * attempt;
        if (snprintf(msg, sizeof(msg), "attempt=%d/%d fetch failed, sleeping %dms", attempt, max_attempts, wait_ms) < 0) {
            copy_string_safe(msg, sizeof(msg), "fetch retry");
        }
        (void)SS_LOG_WARN("backfill.fetch_retry", msg);
        sleep_ms(wait_ms);
        rc = -1;
    }

    return rc;
}

static int append_archive_record(
    ss_sdk_record **records,
    size_t *count,
    size_t *cap,
    ss_metric_id metric,
    int64_t ts_utc,
    double value)
{
    ss_sdk_record rec;

    if (records == NULL || count == NULL || cap == NULL) {
        return -1;
    }

    if (ss_sdk_record_make_f64(&rec, metric, value, ts_utc, SS_SDK_DATA_OBSERVATION) != SS_SDK_OK) {
        return -1;
    }

    if (*count == *cap) {
        size_t next_cap = (*cap == 0U) ? 256U : (*cap * 2U);
        ss_sdk_record *grown;

        if (next_cap < *cap || next_cap > SIZE_MAX / sizeof(**records)) {
            return -1;
        }
        grown = (ss_sdk_record *)realloc(*records, next_cap * sizeof(*grown));
        if (grown == NULL) {
            return -1;
        }
        *records = grown;
        *cap = next_cap;
    }

    (*records)[*count] = rec;
    *count += 1U;
    return 0;
}

static int parse_archive_payload(const char *json_text, cJSON **out_root, archive_hourly_arrays *out_arrays)
{
    cJSON *root;
    if (json_text == NULL || out_root == NULL || out_arrays == NULL) {
        return -1;
    }
    *out_root = NULL;

    root = cJSON_Parse(json_text);
    if (root == NULL) {
        return -1;
    }
    if (archive_extract_arrays(root, out_arrays) != 0) {
        cJSON_Delete(root);
        return -1;
    }

    *out_root = root;
    return 0;
}

static int write_archive_payload(const char *json_text, size_t *out_writes)
{
    cJSON *root = NULL;
    archive_hourly_arrays arrays;
    ss_sdk_record *records = NULL;
    size_t record_count = 0U;
    size_t record_cap = 0U;
    size_t written = 0U;
    int len = 0;
    int i;

    if (json_text == NULL) {
        return -1;
    }

    if (parse_archive_payload(json_text, &root, &arrays) != 0) {
        return -1;
    }

    len = cJSON_GetArraySize(arrays.times);
    for (i = 0; i < len; ++i) {
        cJSON *jt = cJSON_GetArrayItem(arrays.times, i);
        cJSON *jtemp;
        cJSON *jcloud;
        cJSON *jrad;
        int64_t ts_utc;

        if (!cJSON_IsString(jt) || jt->valuestring == NULL) {
            continue;
        }
        if (parse_utc_hour(jt->valuestring, &ts_utc) != 0) {
            continue;
        }
        jtemp = cJSON_GetArrayItem(arrays.temp, i);
        jcloud = cJSON_GetArrayItem(arrays.cloud, i);
        jrad = cJSON_GetArrayItem(arrays.rad, i);
        if (!cJSON_IsNumber(jtemp) || !cJSON_IsNumber(jcloud) || !cJSON_IsNumber(jrad)) {
            continue;
        }
        if (append_archive_record(&records, &record_count, &record_cap, SS_METRIC_WEATHER_TEMPERATURE_AIR_2M_C, ts_utc, jtemp->valuedouble) !=
                0 ||
            append_archive_record(&records, &record_count, &record_cap, SS_METRIC_WEATHER_CLOUD_COVER_TOTAL_PCT, ts_utc, jcloud->valuedouble) !=
                0 ||
            append_archive_record(&records, &record_count, &record_cap, SS_METRIC_WEATHER_RADIATION_SHORTWAVE_WM2, ts_utc, jrad->valuedouble) !=
                0) {
            free(records);
            cJSON_Delete(root);
            return -1;
        }
    }

    if (record_count > 0U) {
        if (ss_sdk_db_write_records(records, record_count, &written) != SS_SDK_OK) {
            free(records);
            cJSON_Delete(root);
            return -1;
        }
    }

    free(records);
    cJSON_Delete(root);
    if (out_writes != NULL) {
        *out_writes = written;
    }
    return 0;
}

static int build_archive_url(char out_url[MAX_URL_LEN], const backfill_config *cfg, int64_t from_utc, int64_t to_utc)
{
    char start_date[11];
    char end_date[11];
    int64_t inclusive_end = to_utc > 0 ? to_utc - 1 : to_utc;
    const char *joiner;
    int n;

    if (out_url == NULL || cfg == NULL || from_utc >= to_utc) {
        return -1;
    }

    if (epoch_to_ymd_utc(from_utc, start_date) != 0) {
        return -1;
    }
    if (epoch_to_ymd_utc(inclusive_end, end_date) != 0) {
        return -1;
    }

    joiner = strchr(cfg->endpoint, '?') != NULL ? "&" : "?";
    n = snprintf(
        out_url,
        MAX_URL_LEN,
        "%s%slatitude=%.6f&longitude=%.6f&start_date=%s&end_date=%s&hourly=temperature_2m,cloud_cover,shortwave_radiation&timezone=UTC",
        cfg->endpoint,
        joiner,
        cfg->latitude,
        cfg->longitude,
        start_date,
        end_date);
    if (n <= 0 || n >= MAX_URL_LEN) {
        return -1;
    }
    return 0;
}

static int detect_holes_window_end(int64_t win_start, int64_t end_utc, int64_t *out_end, uint16_t *out_quarters)
{
    int64_t win_end;
    int64_t span_slots;
    if (out_end == NULL || out_quarters == NULL) {
        return -1;
    }

    win_end = win_start + ((int64_t)MAX_READ_SLOTS * SLOT_SECONDS);
    if (win_end > end_utc) {
        win_end = end_utc;
    }

    span_slots = (win_end - win_start) / SLOT_SECONDS;
    if (span_slots <= 0 || span_slots > UINT16_MAX) {
        return -1;
    }

    *out_end = win_end;
    *out_quarters = (uint16_t)span_slots;
    return 0;
}

static int mark_present_slots(const present_mark_ctx *ctx)
{
    bool *present = NULL;
    size_t i;

    if (ctx == NULL || ctx->samples == NULL || ctx->out_present == NULL) {
        return -1;
    }

    present = (bool *)calloc((size_t)ctx->quarters, sizeof(bool));
    if (present == NULL) {
        return -1;
    }

    for (i = 0U; i < ctx->samples->count; ++i) {
        int64_t ts = ctx->samples->samples[i].ts_utc;
        if (ts >= ctx->win_start && ts < ctx->win_end) {
            size_t idx = (size_t)((ts - ctx->win_start) / SLOT_SECONDS);
            if (idx < (size_t)ctx->quarters) {
                present[idx] = true;
            }
        }
    }

    *ctx->out_present = present;
    return 0;
}

static int push_missing_ranges(int64_t win_start, uint16_t quarters, const bool *present, hole_list *holes)
{
    size_t i = 0U;

    if (present == NULL || holes == NULL) {
        return -1;
    }

    while (i < (size_t)quarters) {
        if (!present[i]) {
            size_t miss_start = i;
            while (i < (size_t)quarters && !present[i]) {
                i += 1U;
            }
            if (!hole_list_push_merge(
                    holes,
                    win_start + ((int64_t)miss_start * SLOT_SECONDS),
                    win_start + ((int64_t)i * SLOT_SECONDS))) {
                return -1;
            }
        } else {
            i += 1U;
        }
    }

    return 0;
}

static int detect_holes_for_metric(ss_metric_id metric, int64_t start_utc, int64_t end_utc, hole_list *holes)
{
    int64_t win_start = start_utc;
    while (win_start < end_utc) {
        int64_t win_end = 0;
        uint16_t quarters;
        ss_sdk_samples_out out = {0};
        bool *present = NULL;
        present_mark_ctx mark_ctx;
        ss_sdk_status st;

        if (detect_holes_window_end(win_start, end_utc, &win_end, &quarters) != 0) {
            break;
        }

        st = ss_sdk_db_get_canonical(win_start, quarters, metric, &out);
        if (st != SS_SDK_OK && st != SS_SDK_ERR_PARTIAL_DATA) {
            ss_sdk_db_free_samples(&out);
            return -1;
        }

        mark_ctx.win_start = win_start;
        mark_ctx.win_end = win_end;
        mark_ctx.quarters = quarters;
        mark_ctx.samples = &out;
        mark_ctx.out_present = &present;
        if (mark_present_slots(&mark_ctx) != 0) {
            ss_sdk_db_free_samples(&out);
            return -1;
        }

        if (push_missing_ranges(win_start, quarters, present, holes) != 0) {
            free(present);
            ss_sdk_db_free_samples(&out);
            return -1;
        }

        free(present);
        ss_sdk_db_free_samples(&out);
        win_start = win_end;
    }
    return 0;
}

static int detect_all_holes(int64_t start_utc, int64_t end_utc, hole_list *holes)
{
    if (detect_holes_for_metric(SS_METRIC_WEATHER_TEMPERATURE_AIR_2M_C, start_utc, end_utc, holes) != 0) {
        return -1;
    }
    if (detect_holes_for_metric(SS_METRIC_WEATHER_CLOUD_COVER_TOTAL_PCT, start_utc, end_utc, holes) != 0) {
        return -1;
    }
    if (detect_holes_for_metric(SS_METRIC_WEATHER_RADIATION_SHORTWAVE_WM2, start_utc, end_utc, holes) != 0) {
        return -1;
    }
    return 0;
}

typedef struct {
    const backfill_config *cfg;
    rate_limiter *rl;
    size_t hole_index;
    size_t hole_count;
    size_t *records_written;
    time_t *last_progress;
} backfill_hole_ctx;

static void backfill_log_progress_if_due(
    const backfill_hole_ctx *ctx,
    size_t records_written)
{
    char msg[256];
    time_t now_ts = time(NULL);

    if (ctx == NULL || ctx->cfg == NULL || ctx->last_progress == NULL) {
        return;
    }
    if ((int)(now_ts - *(ctx->last_progress)) < ctx->cfg->progress_log_interval_sec) {
        return;
    }

    if (snprintf(msg, sizeof(msg), "progress hole=%zu/%zu records_written=%zu", ctx->hole_index, ctx->hole_count, records_written) < 0) {
        copy_string_safe(msg, sizeof(msg), "backfill progress");
    }
    (void)SS_LOG_INFO("backfill.progress", msg);
    *(ctx->last_progress) = now_ts;
}

static int backfill_process_chunk(
    backfill_hole_ctx *ctx,
    int64_t part_start,
    int64_t next_end)
{
    char url[MAX_URL_LEN];
    char *buf = NULL;
    size_t wrote_now = 0U;

    if (ctx == NULL || ctx->cfg == NULL || ctx->rl == NULL || ctx->records_written == NULL) {
        return -1;
    }
    if (build_archive_url(url, ctx->cfg, part_start, next_end) != 0) {
        return -1;
    }

    rate_limiter_maybe_wait(ctx->rl, ctx->cfg);
    if (fetch_json_with_retry(url, ctx->cfg->retry_max_attempts, ctx->cfg->retry_base_backoff_ms, &buf) != 0) {
        return -1;
    }

    if (write_archive_payload(buf, &wrote_now) != 0) {
        free(buf);
        return -1;
    }
    free(buf);

    *(ctx->records_written) += wrote_now;
    sleep_ms(ctx->cfg->request_interval_ms);
    return 0;
}

static int backfill_process_hole(backfill_hole_ctx *ctx, const hole_range *hole)
{
    int64_t chunk_seconds;
    int64_t part_start;
    int64_t part_end;

    if (ctx == NULL || ctx->cfg == NULL || hole == NULL) {
        return -1;
    }

    chunk_seconds = (int64_t)ctx->cfg->chunk_days * 86400;
    part_start = hole->from_utc;
    part_end = hole->to_utc;

    while (part_start < part_end) {
        int64_t next_end = part_start + chunk_seconds;
        if (next_end > part_end) {
            next_end = part_end;
        }

        if (backfill_process_chunk(ctx, part_start, next_end) != 0) {
            return -1;
        }

        part_start = next_end;
        backfill_log_progress_if_due(ctx, *(ctx->records_written));
    }

    return 0;
}

static int backfill_holes(const backfill_config *cfg, const hole_list *holes, size_t *out_records_written)
{
    size_t i;
    size_t records_written = 0U;
    time_t last_progress = 0;
    rate_limiter rl;
    backfill_hole_ctx ctx;

    memset(&rl, 0, sizeof(rl));
    rl.minute_epoch = -1;
    rl.hour_epoch = -1;
    rl.day_epoch = -1;

    ctx.cfg = cfg;
    ctx.rl = &rl;
    ctx.hole_count = holes->count;
    ctx.records_written = &records_written;
    ctx.last_progress = &last_progress;

    for (i = 0U; i < holes->count; ++i) {
        ctx.hole_index = i + 1U;
        if (backfill_process_hole(&ctx, &holes->items[i]) != 0) {
            return -1;
        }
    }

    if (out_records_written != NULL) {
        *out_records_written = records_written;
    }
    return 0;
}

static int backfill_analyze_window(int64_t start_utc, int64_t end_utc, hole_list *before)
{
    char msg[256];

    if (detect_all_holes(start_utc, end_utc, before) != 0) {
        (void)SS_LOG_ERROR("backfill.detect_failed", "failed to analyze current DB holes");
        return -1;
    }
    if (snprintf(msg, sizeof(msg), "window_start=%" PRId64 " window_end=%" PRId64 " holes_before=%zu", start_utc, end_utc, before->count) <
        0) {
        copy_string_safe(msg, sizeof(msg), "backfill analyze");
    }
    (void)SS_LOG_INFO("backfill.analyze", msg);
    return 0;
}

static int backfill_verify_window(int64_t start_utc, int64_t end_utc, size_t records_written, hole_list *after)
{
    char msg[256];

    if (detect_all_holes(start_utc, end_utc, after) != 0) {
        (void)SS_LOG_ERROR("backfill.verify_failed", "failed to verify DB completeness");
        return -1;
    }

    if (snprintf(msg, sizeof(msg), "records_written=%zu holes_after=%zu", records_written, after->count) < 0) {
        copy_string_safe(msg, sizeof(msg), "backfill verify");
    }
    if (after->count == 0U) {
        (void)SS_LOG_INFO("backfill.complete", msg);
        return 0;
    }
    (void)SS_LOG_WARN("backfill.partial", msg);
    return 1;
}

static int run_backfill_once(const backfill_config *cfg)
{
    int64_t end_utc = align_to_slot(now_utc() - ((int64_t)cfg->freshness_lag_minutes * 60));
    int64_t start_utc = align_to_slot(end_utc - ((int64_t)cfg->required_history_days * 86400));
    hole_list before = {0};
    hole_list after = {0};
    size_t records_written = 0U;

    if (start_utc >= end_utc) {
        (void)SS_LOG_WARN("backfill.window_invalid", "computed window invalid");
        return 1;
    }

    if (backfill_analyze_window(start_utc, end_utc, &before) != 0) {
        hole_list_free(&before);
        return 1;
    }

    if (before.count > 0U) {
        if (backfill_holes(cfg, &before, &records_written) != 0) {
            (void)SS_LOG_ERROR("backfill.fill_failed", "fetch/write loop failed");
            hole_list_free(&before);
            return 1;
        }
    }
    hole_list_free(&before);

    {
        int verify_rc = backfill_verify_window(start_utc, end_utc, records_written, &after);
        if (verify_rc < 0) {
            hole_list_free(&after);
            return 1;
        }
        if (verify_rc == 0) {
            hole_list_free(&after);
            return 0;
        }
    }
    hole_list_free(&after);
    return 1;
}

int main(void)
{
    backfill_config cfg;

    backfill_config_parse(&cfg);
    log_cfg(&cfg);

    if (!cfg.enabled) {
        (void)SS_LOG_INFO("backfill.disabled", "backfill disabled by config");
        ss_sdk_shutdown();
        return EXIT_SUCCESS;
    }

    if (!cfg.has_system_location) {
        (void)SS_LOG_ERROR("backfill.config.invalid_location", "missing system.location latitude/longitude");
        ss_sdk_shutdown();
        return EXIT_FAILURE;
    }

    if (run_backfill_once(&cfg) != 0) {
        ss_sdk_shutdown();
        return EXIT_FAILURE;
    }

    ss_sdk_shutdown();
    return EXIT_SUCCESS;
}
