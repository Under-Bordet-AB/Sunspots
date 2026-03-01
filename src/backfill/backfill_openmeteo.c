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
    int daily_hole_check_interval_sec;
    int freshness_lag_minutes;
    int request_interval_ms;
    int max_requests_per_minute;
    int max_requests_per_hour;
    int max_requests_per_day;
    double latitude;
    double longitude;
    char mode[16];
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
    cfg->daily_hole_check_interval_sec = 86400;
    cfg->freshness_lag_minutes = 120;
    cfg->request_interval_ms = 250;
    cfg->max_requests_per_minute = 240;
    cfg->max_requests_per_hour = 2000;
    cfg->max_requests_per_day = 8000;
    cfg->latitude = 52.52;
    cfg->longitude = 13.41;
    snprintf(cfg->mode, sizeof(cfg->mode), "%s", "oneshot");
    snprintf(cfg->endpoint, sizeof(cfg->endpoint), "%s", "https://archive-api.open-meteo.com/v1/archive");
}

static void backfill_config_parse(backfill_config *cfg)
{
    const char *blob;
    cJSON *root = NULL;
    cJSON *bf = NULL;
    cJSON *item;

    backfill_config_defaults(cfg);

    blob = getenv("SUNSPOTS_CONFIG");
    if (blob == NULL || blob[0] == '\0') {
        return;
    }

    root = cJSON_Parse(blob);
    if (root == NULL) {
        return;
    }

    bf = cJSON_GetObjectItemCaseSensitive(root, "backfill");
    if (!cJSON_IsObject(bf)) {
        bf = root;
    }

    item = cJSON_GetObjectItemCaseSensitive(bf, "enabled");
    if (cJSON_IsBool(item)) {
        cfg->enabled = cJSON_IsTrue(item) ? 1 : 0;
    }

    item = cJSON_GetObjectItemCaseSensitive(bf, "required_history_days");
    if (cJSON_IsNumber(item)) {
        cfg->required_history_days = clamp_min_int(item->valueint, 1, cfg->required_history_days);
    }

    item = cJSON_GetObjectItemCaseSensitive(bf, "chunk_days");
    if (cJSON_IsNumber(item)) {
        cfg->chunk_days = clamp_min_int(item->valueint, 1, cfg->chunk_days);
    }

    item = cJSON_GetObjectItemCaseSensitive(bf, "retry_max_attempts");
    if (cJSON_IsNumber(item)) {
        cfg->retry_max_attempts = clamp_min_int(item->valueint, 1, cfg->retry_max_attempts);
    }

    item = cJSON_GetObjectItemCaseSensitive(bf, "retry_base_backoff_ms");
    if (cJSON_IsNumber(item)) {
        cfg->retry_base_backoff_ms = clamp_min_int(item->valueint, 100, cfg->retry_base_backoff_ms);
    }

    item = cJSON_GetObjectItemCaseSensitive(bf, "progress_log_interval_sec");
    if (cJSON_IsNumber(item)) {
        cfg->progress_log_interval_sec = clamp_min_int(item->valueint, 1, cfg->progress_log_interval_sec);
    }

    item = cJSON_GetObjectItemCaseSensitive(bf, "daily_hole_check_interval_sec");
    if (cJSON_IsNumber(item)) {
        cfg->daily_hole_check_interval_sec = clamp_min_int(item->valueint, 60, cfg->daily_hole_check_interval_sec);
    }

    item = cJSON_GetObjectItemCaseSensitive(bf, "freshness_lag_minutes");
    if (cJSON_IsNumber(item)) {
        cfg->freshness_lag_minutes = clamp_min_int(item->valueint, 0, cfg->freshness_lag_minutes);
    }

    item = cJSON_GetObjectItemCaseSensitive(bf, "request_interval_ms");
    if (cJSON_IsNumber(item)) {
        cfg->request_interval_ms = clamp_min_int(item->valueint, 50, cfg->request_interval_ms);
    }

    item = cJSON_GetObjectItemCaseSensitive(bf, "max_requests_per_minute");
    if (cJSON_IsNumber(item)) {
        cfg->max_requests_per_minute = clamp_min_int(item->valueint, 1, cfg->max_requests_per_minute);
    }
    if (cfg->max_requests_per_minute > OPENMETEO_LIMIT_PER_MIN) {
        cfg->max_requests_per_minute = OPENMETEO_LIMIT_PER_MIN;
    }

    item = cJSON_GetObjectItemCaseSensitive(bf, "max_requests_per_hour");
    if (cJSON_IsNumber(item)) {
        cfg->max_requests_per_hour = clamp_min_int(item->valueint, 1, cfg->max_requests_per_hour);
    }
    if (cfg->max_requests_per_hour > OPENMETEO_LIMIT_PER_HOUR) {
        cfg->max_requests_per_hour = OPENMETEO_LIMIT_PER_HOUR;
    }

    item = cJSON_GetObjectItemCaseSensitive(bf, "max_requests_per_day");
    if (cJSON_IsNumber(item)) {
        cfg->max_requests_per_day = clamp_min_int(item->valueint, 1, cfg->max_requests_per_day);
    }
    if (cfg->max_requests_per_day > OPENMETEO_LIMIT_PER_DAY) {
        cfg->max_requests_per_day = OPENMETEO_LIMIT_PER_DAY;
    }

    item = cJSON_GetObjectItemCaseSensitive(bf, "latitude");
    if (cJSON_IsNumber(item)) {
        cfg->latitude = item->valuedouble;
    }

    item = cJSON_GetObjectItemCaseSensitive(bf, "longitude");
    if (cJSON_IsNumber(item)) {
        cfg->longitude = item->valuedouble;
    }

    item = cJSON_GetObjectItemCaseSensitive(bf, "mode");
    if (cJSON_IsString(item) && item->valuestring != NULL) {
        snprintf(cfg->mode, sizeof(cfg->mode), "%s", item->valuestring);
    }

    item = cJSON_GetObjectItemCaseSensitive(bf, "endpoint");
    if (cJSON_IsString(item) && item->valuestring != NULL) {
        snprintf(cfg->endpoint, sizeof(cfg->endpoint), "%s", item->valuestring);
    }

    cJSON_Delete(root);
}

static void log_cfg(const backfill_config *cfg)
{
    char msg[256];
    snprintf(msg,
             sizeof(msg),
             "enabled=%d mode=%s history_days=%d chunk_days=%d retries=%d backoff_ms=%d lag_min=%d req_interval_ms=%d limit_m=%d limit_h=%d limit_d=%d lat=%.4f lon=%.4f endpoint=%.80s",
             cfg->enabled,
             cfg->mode,
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
             cfg->endpoint);
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
    if (sec_to_minute <= 0) {
        sec_to_minute = 1;
    }
    sec_to_hour = 3600 - (int)(now_utc % 3600);
    if (sec_to_hour <= 0) {
        sec_to_hour = 1;
    }
    sec_to_day = 86400 - (int)(now_utc % 86400);
    if (sec_to_day <= 0) {
        sec_to_day = 1;
    }

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
        snprintf(msg, sizeof(msg), "throttling wait=%ds", wait_sec);
        (void)SS_LOG_WARN("backfill.rate_limit_wait", msg);
        sleep_ms(wait_sec * 1000);
    }
}

static int fetch_json_with_retry(const char *url, int max_attempts, int base_backoff_ms, char **out_buf)
{
    int attempt;
    int wait_ms;
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
        wait_ms = base_backoff_ms * attempt;
        snprintf(msg, sizeof(msg), "attempt=%d/%d fetch failed, sleeping %dms", attempt, max_attempts, wait_ms);
        (void)SS_LOG_WARN("backfill.fetch_retry", msg);
        sleep_ms(wait_ms);
        rc = -1;
    }

    return rc;
}

static int write_f64_metric(ss_metric_id metric, int64_t ts_utc, double v)
{
    ss_sdk_record rec;
    ss_sdk_status st = ss_sdk_record_make_f64(&rec, metric, v, ts_utc, SS_SDK_DATA_OBSERVATION);
    if (st != SS_SDK_OK) {
        return -1;
    }
    st = ss_sdk_db_write_record(&rec);
    return st == SS_SDK_OK ? 0 : -1;
}

static int write_archive_payload(const char *json_text, size_t *out_writes)
{
    cJSON *root = NULL;
    cJSON *hourly = NULL;
    cJSON *times = NULL;
    cJSON *temp = NULL;
    cJSON *cloud = NULL;
    cJSON *rad = NULL;
    int len = 0;
    int i;
    size_t writes = 0U;

    if (json_text == NULL) {
        return -1;
    }

    root = cJSON_Parse(json_text);
    if (root == NULL) {
        return -1;
    }

    hourly = cJSON_GetObjectItemCaseSensitive(root, "hourly");
    if (!cJSON_IsObject(hourly)) {
        cJSON_Delete(root);
        return -1;
    }

    times = cJSON_GetObjectItemCaseSensitive(hourly, "time");
    temp = cJSON_GetObjectItemCaseSensitive(hourly, "temperature_2m");
    cloud = cJSON_GetObjectItemCaseSensitive(hourly, "cloud_cover");
    rad = cJSON_GetObjectItemCaseSensitive(hourly, "shortwave_radiation");
    if (!cJSON_IsArray(times)) {
        cJSON_Delete(root);
        return -1;
    }

    len = cJSON_GetArraySize(times);
    for (i = 0; i < len; ++i) {
        cJSON *jt = cJSON_GetArrayItem(times, i);
        int64_t ts_utc;

        if (!cJSON_IsString(jt) || jt->valuestring == NULL) {
            continue;
        }
        if (parse_utc_hour(jt->valuestring, &ts_utc) != 0) {
            continue;
        }

        if (cJSON_IsArray(temp)) {
            cJSON *v = cJSON_GetArrayItem(temp, i);
            if (cJSON_IsNumber(v) && write_f64_metric(SS_METRIC_WEATHER_TEMPERATURE_AIR_2M_C, ts_utc, v->valuedouble) == 0) {
                writes += 1U;
            }
        }
        if (cJSON_IsArray(cloud)) {
            cJSON *v = cJSON_GetArrayItem(cloud, i);
            if (cJSON_IsNumber(v) && write_f64_metric(SS_METRIC_WEATHER_CLOUD_COVER_TOTAL_PCT, ts_utc, v->valuedouble) == 0) {
                writes += 1U;
            }
        }
        if (cJSON_IsArray(rad)) {
            cJSON *v = cJSON_GetArrayItem(rad, i);
            if (cJSON_IsNumber(v) && write_f64_metric(SS_METRIC_WEATHER_RADIATION_SHORTWAVE_WM2, ts_utc, v->valuedouble) == 0) {
                writes += 1U;
            }
        }
    }

    cJSON_Delete(root);
    if (out_writes != NULL) {
        *out_writes = writes;
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

static int detect_holes_for_metric(ss_metric_id metric, int64_t start_utc, int64_t end_utc, hole_list *holes)
{
    int64_t win_start = start_utc;
    while (win_start < end_utc) {
        int64_t win_end;
        int64_t span_slots;
        uint16_t quarters;
        ss_sdk_samples_out out = {0};
        bool *present = NULL;
        size_t i;
        ss_sdk_status st;

        win_end = win_start + ((int64_t)MAX_READ_SLOTS * SLOT_SECONDS);
        if (win_end > end_utc) {
            win_end = end_utc;
        }
        span_slots = (win_end - win_start) / SLOT_SECONDS;
        quarters = (uint16_t)span_slots;
        if (quarters == 0U) {
            break;
        }

        st = ss_sdk_db_get_canonical(win_start, quarters, metric, &out);
        if (st != SS_SDK_OK && st != SS_SDK_ERR_PARTIAL_DATA) {
            ss_sdk_db_free_samples(&out);
            return -1;
        }

        present = (bool *)calloc((size_t)quarters, sizeof(bool));
        if (present == NULL) {
            ss_sdk_db_free_samples(&out);
            return -1;
        }

        for (i = 0U; i < out.count; ++i) {
            int64_t ts = out.samples[i].ts_utc;
            if (ts >= win_start && ts < win_end) {
                size_t idx = (size_t)((ts - win_start) / SLOT_SECONDS);
                if (idx < (size_t)quarters) {
                    present[idx] = true;
                }
            }
        }

        i = 0U;
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
                    free(present);
                    ss_sdk_db_free_samples(&out);
                    return -1;
                }
            } else {
                i += 1U;
            }
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

static int backfill_holes(const backfill_config *cfg, const hole_list *holes, size_t *out_records_written)
{
    size_t i;
    size_t records_written = 0U;
    time_t last_progress = 0;
    char msg[256];
    int64_t chunk_seconds = (int64_t)cfg->chunk_days * 86400;
    rate_limiter rl;

    memset(&rl, 0, sizeof(rl));
    rl.minute_epoch = -1;
    rl.hour_epoch = -1;
    rl.day_epoch = -1;

    for (i = 0U; i < holes->count; ++i) {
        int64_t part_start = holes->items[i].from_utc;
        int64_t part_end = holes->items[i].to_utc;

        while (part_start < part_end) {
            int64_t next_end = part_start + chunk_seconds;
            char url[MAX_URL_LEN];
            char *buf = NULL;
            size_t wrote_now = 0U;

            if (next_end > part_end) {
                next_end = part_end;
            }

            if (build_archive_url(url, cfg, part_start, next_end) != 0) {
                return -1;
            }

            rate_limiter_maybe_wait(&rl, cfg);
            if (fetch_json_with_retry(url, cfg->retry_max_attempts, cfg->retry_base_backoff_ms, &buf) != 0) {
                return -1;
            }

            if (write_archive_payload(buf, &wrote_now) != 0) {
                free(buf);
                return -1;
            }
            free(buf);
            records_written += wrote_now;
            part_start = next_end;

            if ((int)(time(NULL) - last_progress) >= cfg->progress_log_interval_sec) {
                snprintf(msg, sizeof(msg), "progress hole=%zu/%zu records_written=%zu", i + 1U, holes->count, records_written);
                (void)SS_LOG_INFO("backfill.progress", msg);
                last_progress = time(NULL);
            }

            sleep_ms(cfg->request_interval_ms);
        }
    }

    if (out_records_written != NULL) {
        *out_records_written = records_written;
    }
    return 0;
}

static int run_backfill_once(const backfill_config *cfg)
{
    int64_t end_utc = align_to_slot(now_utc() - ((int64_t)cfg->freshness_lag_minutes * 60));
    int64_t start_utc = align_to_slot(end_utc - ((int64_t)cfg->required_history_days * 86400));
    hole_list before = {0};
    hole_list after = {0};
    char msg[256];
    size_t records_written = 0U;

    if (start_utc >= end_utc) {
        (void)SS_LOG_WARN("backfill.window_invalid", "computed window invalid");
        return 1;
    }

    if (detect_all_holes(start_utc, end_utc, &before) != 0) {
        (void)SS_LOG_ERROR("backfill.detect_failed", "failed to analyze current DB holes");
        hole_list_free(&before);
        return 1;
    }

    snprintf(msg, sizeof(msg), "window_start=%" PRId64 " window_end=%" PRId64 " holes_before=%zu", start_utc, end_utc, before.count);
    (void)SS_LOG_INFO("backfill.analyze", msg);

    if (before.count > 0U) {
        if (backfill_holes(cfg, &before, &records_written) != 0) {
            (void)SS_LOG_ERROR("backfill.fill_failed", "fetch/write loop failed");
            hole_list_free(&before);
            return 1;
        }
    }
    hole_list_free(&before);

    if (detect_all_holes(start_utc, end_utc, &after) != 0) {
        (void)SS_LOG_ERROR("backfill.verify_failed", "failed to verify DB completeness");
        hole_list_free(&after);
        return 1;
    }

    snprintf(msg, sizeof(msg), "records_written=%zu holes_after=%zu", records_written, after.count);
    if (after.count == 0U) {
        (void)SS_LOG_INFO("backfill.complete", msg);
        hole_list_free(&after);
        return 0;
    } else {
        (void)SS_LOG_WARN("backfill.partial", msg);
        hole_list_free(&after);
        return 1;
    }
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

    if (strcmp(cfg.mode, "maintenance") == 0) {
        for (;;) {
            (void)run_backfill_once(&cfg);
            sleep_ms(cfg.daily_hole_check_interval_sec * 1000);
        }
    }

    if (run_backfill_once(&cfg) != 0) {
        ss_sdk_shutdown();
        return EXIT_FAILURE;
    }

    ss_sdk_shutdown();
    return EXIT_SUCCESS;
}
