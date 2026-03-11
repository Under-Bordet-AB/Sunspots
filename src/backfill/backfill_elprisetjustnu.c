#define _GNU_SOURCE
#include <errno.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <unistd.h>
#include <curl/curl.h>

#include "../sdk/ss_sdk.h"
#include "backfill_common.h"
#include "backfill_config.h"
#include "backfill_price_payload.h"

typedef struct {
    int minute_epoch;
    int hour_epoch;
    int day_epoch;
    int minute_count;
    int hour_count;
    int day_count;
} rate_limiter;

typedef struct {
    char date[11];
    int64_t count;
} usage_day_count;

typedef struct {
    char *data;
    size_t size;
} http_buffer;

static int64_t now_utc(void)
{
    return (int64_t)time(NULL);
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

static size_t http_write_callback(void *contents, size_t size, size_t nmemb, void *userp)
{
    http_buffer *buffer = (http_buffer *)userp;
    size_t chunk_size = size * nmemb;
    char *grown;

    if (buffer == NULL || chunk_size == 0U) {
        return chunk_size;
    }
    grown = (char *)realloc(buffer->data, buffer->size + chunk_size + 1U);
    if (grown == NULL) {
        return 0U;
    }
    buffer->data = grown;
    memcpy(buffer->data + buffer->size, contents, chunk_size);
    buffer->size += chunk_size;
    buffer->data[buffer->size] = '\0';
    return chunk_size;
}

static int fetch_price_json_once(const char *url, char **out_buf)
{
    static const char user_agent[] = "SunspotsBackfillElprisjustnu/1.0 (+https://www.elprisetjustnu.se/)";
    CURL *curl = NULL;
    CURLcode code;
    long http_status = 0L;
    http_buffer buffer;

    if (url == NULL || out_buf == NULL) {
        return -1;
    }
    *out_buf = NULL;
    memset(&buffer, 0, sizeof(buffer));

    if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) {
        return -1;
    }
    curl = curl_easy_init();
    if (curl == NULL) {
        curl_global_cleanup();
        return -1;
    }

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, user_agent);
    curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, http_write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buffer);
    curl_easy_setopt(curl, CURLOPT_FAILONERROR, 0L);

    code = curl_easy_perform(curl);
    if (code == CURLE_OK) {
        (void)curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_status);
    }
    curl_easy_cleanup(curl);
    curl_global_cleanup();

    if (code != CURLE_OK || http_status < 200L || http_status >= 300L || buffer.data == NULL) {
        free(buffer.data);
        return -1;
    }

    *out_buf = buffer.data;
    return 0;
}

static int usage_day_today(char out_date[11])
{
    time_t nowv;
    struct tm tmv;

    if (out_date == NULL) {
        return -1;
    }
    nowv = time(NULL);
    if (gmtime_r(&nowv, &tmv) == NULL) {
        return -1;
    }
    if (strftime(out_date, 11, "%Y-%m-%d", &tmv) != 10U) {
        return -1;
    }
    return 0;
}

static int usage_day_cutoff(char out_date[11])
{
    time_t nowv;
    struct tm tmv;

    if (out_date == NULL) {
        return -1;
    }
    nowv = time(NULL) - (time_t)(BACKFILL_USAGE_KEEP_DAYS - 1) * 86400;
    if (gmtime_r(&nowv, &tmv) == NULL) {
        return -1;
    }
    if (strftime(out_date, 11, "%Y-%m-%d", &tmv) != 10U) {
        return -1;
    }
    return 0;
}

static int parse_usage_day_count_line(const char *line, usage_day_count *out_row)
{
    char date[11];
    char count_buf[32];
    char trailing[2];
    long long count_ll;
    char *endp = NULL;

    if (line == NULL || out_row == NULL) {
        return -1;
    }
    if (sscanf(line, "%10s %31s %1s", date, count_buf, trailing) != 2) {
        return -1;
    }
    count_ll = strtoll(count_buf, &endp, 10);
    if (endp == NULL || *endp != '\0') {
        return -1;
    }
    backfill_copy_string_safe(out_row->date, sizeof(out_row->date), date);
    out_row->count = (int64_t)count_ll;
    return 0;
}

static size_t usage_load_rows(FILE *fp, const char cutoff[11], usage_day_count *rows, size_t rows_cap)
{
    size_t row_count = 0U;
    char line[128];

    if (fp == NULL || cutoff == NULL || rows == NULL || rows_cap == 0U) {
        return 0U;
    }
    while (fgets(line, sizeof(line), fp) != NULL) {
        usage_day_count row;

        if (parse_usage_day_count_line(line, &row) != 0) {
            continue;
        }
        if (strcmp(row.date, cutoff) < 0 || row_count >= rows_cap) {
            continue;
        }
        rows[row_count] = row;
        row_count += 1U;
    }
    return row_count;
}

static size_t usage_upsert_today(usage_day_count *rows, size_t row_count, size_t rows_cap, const char today[11], int delta)
{
    size_t i;

    for (i = 0U; i < row_count; ++i) {
        if (strcmp(rows[i].date, today) == 0) {
            rows[i].count += (int64_t)delta;
            return row_count;
        }
    }
    if (row_count < rows_cap) {
        backfill_copy_string_safe(rows[row_count].date, sizeof(rows[row_count].date), today);
        rows[row_count].count = (int64_t)delta;
        row_count += 1U;
    }
    return row_count;
}

static void usage_bump_daily_count(const char *path, int delta)
{
    usage_day_count rows[BACKFILL_USAGE_KEEP_DAYS + 8];
    size_t row_count = 0U;
    char today[11];
    char cutoff[11];
    FILE *fp;
    size_t i;
    const char *use_path = path;

    if (delta <= 0) {
        return;
    }
    if (use_path == NULL || use_path[0] == '\0') {
        use_path = BACKFILL_USAGE_DAILY_PATH;
    }
    if (usage_day_today(today) != 0 || usage_day_cutoff(cutoff) != 0) {
        return;
    }

    (void)mkdir("logs", 0775);
    fp = fopen(use_path, "r");
    if (fp != NULL) {
        row_count = usage_load_rows(fp, cutoff, rows, sizeof(rows) / sizeof(rows[0]));
        (void)fclose(fp);
    }
    row_count = usage_upsert_today(rows, row_count, sizeof(rows) / sizeof(rows[0]), today, delta);
    while (row_count > BACKFILL_USAGE_KEEP_DAYS) {
        memmove(&rows[0], &rows[1], (row_count - 1U) * sizeof(rows[0]));
        row_count -= 1U;
    }

    fp = fopen(use_path, "w");
    if (fp == NULL) {
        return;
    }
    for (i = 0U; i < row_count; ++i) {
        (void)fprintf(fp, "%s %" PRId64 "\n", rows[i].date, rows[i].count);
    }
    (void)fclose(fp);
}

static int floor_div_pos(int64_t num, int denom)
{
    if (denom <= 0 || num < 0) {
        return 0;
    }
    return (int)(num / denom);
}

static int compute_wait_seconds(int64_t nowv, bool hit_minute, bool hit_hour, bool hit_day)
{
    int sec_to_minute = 60 - (int)(nowv % 60);
    int sec_to_hour = 3600 - (int)(nowv % 3600);
    int sec_to_day = 86400 - (int)(nowv % 86400);
    int wait_sec = 0;

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

static void rate_limiter_init(rate_limiter *rl)
{
    if (rl == NULL) {
        return;
    }
    memset(rl, 0, sizeof(*rl));
    rl->minute_epoch = -1;
    rl->hour_epoch = -1;
    rl->day_epoch = -1;
}

static void rate_limiter_maybe_wait(rate_limiter *rl, const backfill_config *cfg)
{
    for (;;) {
        int64_t nowv = now_utc();
        int minute_epoch = floor_div_pos(nowv, 60);
        int hour_epoch = floor_div_pos(nowv, 3600);
        int day_epoch = floor_div_pos(nowv, 86400);
        bool hit_minute;
        bool hit_hour;
        bool hit_day;

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

        hit_minute = rl->minute_count >= cfg->max_requests_per_minute;
        hit_hour = rl->hour_count >= cfg->max_requests_per_hour;
        hit_day = rl->day_count >= cfg->max_requests_per_day;
        if (!hit_minute && !hit_hour && !hit_day) {
            rl->minute_count += 1;
            rl->hour_count += 1;
            rl->day_count += 1;
            usage_bump_daily_count(cfg->usage_daily_path, 1);
            return;
        }

        sleep_ms(compute_wait_seconds(nowv, hit_minute, hit_hour, hit_day) * 1000);
    }
}

static int fetch_json_with_retry(const char *url, int max_attempts, int base_backoff_ms, char **out_buf)
{
    int attempt;

    if (url == NULL || out_buf == NULL) {
        return -1;
    }
    *out_buf = NULL;

    for (attempt = 1; attempt <= max_attempts; ++attempt) {
        char *buf = NULL;

        if (fetch_price_json_once(url, &buf) == 0 && buf != NULL) {
            *out_buf = buf;
            return 0;
        }
        free(buf);
        sleep_ms(base_backoff_ms * attempt);
    }
    return -1;
}

static int day_start_utc(int64_t ts_utc, int64_t *out_day_start_utc)
{
    char ymd[11];

    if (out_day_start_utc == NULL) {
        return -1;
    }
    if (backfill_epoch_to_ymd_utc(ts_utc, ymd) != 0) {
        return -1;
    }
    return backfill_parse_utc_date(ymd, out_day_start_utc);
}

static int tomorrow_day_start_utc(int64_t *out_day_start_utc)
{
    time_t nowv;
    struct tm tmv;

    if (out_day_start_utc == NULL) {
        return -1;
    }
    nowv = time(NULL);
    if (localtime_r(&nowv, &tmv) == NULL) {
        return -1;
    }
    tmv.tm_hour = 0;
    tmv.tm_min = 0;
    tmv.tm_sec = 0;
    tmv.tm_mday += 1;
    nowv = mktime(&tmv);
    if (nowv == (time_t)-1) {
        return -1;
    }
    return day_start_utc((int64_t)nowv, out_day_start_utc);
}

static int64_t max_i64(int64_t a, int64_t b)
{
    return (a > b) ? a : b;
}

static int determine_fetch_start_utc(int64_t configured_start_utc, int64_t *out_start_utc)
{
    ss_sdk_samples_out out = {0};
    ss_sdk_status st;
    int64_t cursor = configured_start_utc;
    int64_t fetch_start_utc = configured_start_utc;
    size_t i;

    if (out_start_utc == NULL) {
        return -1;
    }

    st = ss_sdk_db_get_canonical(configured_start_utc, 0, SS_METRIC_ENERGY_PRICE_SPOT_SEK_KWH, &out);
    if (st != SS_SDK_OK &&
        st != SS_SDK_CLAMPED &&
        st != SS_SDK_CLAMPED_PARTIAL_DATA &&
        st != SS_SDK_ERR_PARTIAL_DATA) {
        ss_sdk_db_free_samples(&out);
        *out_start_utc = configured_start_utc;
        return 0;
    }
    if (out.count == 0U) {
        ss_sdk_db_free_samples(&out);
        *out_start_utc = configured_start_utc;
        return 0;
    }

    for (i = 0U; i < out.count; ++i) {
        int64_t ts_utc = out.samples[i].ts_utc;

        if (ts_utc < cursor) {
            continue;
        }
        if (ts_utc > cursor) {
            fetch_start_utc = cursor;
            break;
        }
        cursor = ts_utc + BACKFILL_SLOT_SECONDS;
        fetch_start_utc = ts_utc;
    }
    ss_sdk_db_free_samples(&out);
    if (day_start_utc(fetch_start_utc, out_start_utc) != 0) {
        *out_start_utc = configured_start_utc;
    }
    return 0;
}

static int fetch_and_write_day(const backfill_config *cfg, rate_limiter *rl, int64_t day_utc, size_t *io_records_written)
{
    char url[BACKFILL_MAX_URL_LEN];
    char msg[256];
    char *buf = NULL;
    size_t wrote_now = 0U;
    time_t tv;
    struct tm tmv;

    if (cfg == NULL || rl == NULL || io_records_written == NULL) {
        return -1;
    }
    tv = (time_t)day_utc;
    if (gmtime_r(&tv, &tmv) == NULL) {
        return -1;
    }
    if (backfill_build_elprisjustnu_day_url(url, cfg, tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday) != 0) {
        return -1;
    }
    if (snprintf(msg, sizeof(msg), "date=%04d-%02d-%02d area=%s", tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday, cfg->elprisomrade) >= 0) {
        (void)SS_LOG_DEBUG("backfill.price_day_fetch_start", msg);
    }

    rate_limiter_maybe_wait(rl, cfg);
    if (fetch_json_with_retry(url, cfg->retry_max_attempts, cfg->retry_base_backoff_ms, &buf) != 0) {
        if (snprintf(msg, sizeof(msg), "date=%04d-%02d-%02d area=%s", tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday, cfg->elprisomrade) >= 0) {
            (void)SS_LOG_WARN("backfill.price_day_fetch_failed", msg);
        }
        return 0;
    }
    if (backfill_write_elprisjustnu_payload(buf, day_utc, &wrote_now) != 0) {
        free(buf);
        if (snprintf(msg, sizeof(msg), "date=%04d-%02d-%02d area=%s", tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday, cfg->elprisomrade) >= 0) {
            (void)SS_LOG_WARN("backfill.price_day_write_failed", msg);
        }
        return 0;
    }
    free(buf);
    *io_records_written += wrote_now;
    if (snprintf(msg, sizeof(msg), "date=%04d-%02d-%02d rows_written=%zu total_rows=%zu", tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday, wrote_now, *io_records_written) >= 0) {
        (void)SS_LOG_DEBUG("backfill.price_day_fetch_complete", msg);
    }
    sleep_ms(cfg->request_interval_ms);
    return 0;
}

static void log_cfg(const backfill_config *cfg)
{
    char msg[384];

    if (snprintf(
            msg,
            sizeof(msg),
            "enabled=%d area=%s perfmode=%s start_date=%s nice=%d retries=%d backoff_ms=%d req_interval_ms=%d limit_m=%d limit_h=%d limit_d=%d endpoint=%.120s",
            cfg->enabled,
            cfg->elprisomrade,
            cfg->perfmode,
            cfg->start_date_utc,
            cfg->process_nice,
            cfg->retry_max_attempts,
            cfg->retry_base_backoff_ms,
            cfg->request_interval_ms,
            cfg->max_requests_per_minute,
            cfg->max_requests_per_hour,
            cfg->max_requests_per_day,
            cfg->endpoint) >= 0) {
        (void)SS_LOG_INFO("backfill.price.config", msg);
    }
}

static int run_price_backfill_once(const backfill_config *cfg)
{
    int64_t configured_start_utc;
    int64_t fetch_start_utc;
    int64_t end_day_utc;
    int64_t day_utc;
    size_t records_written = 0U;
    size_t day_count = 0U;
    rate_limiter rl;
    char msg[256];
    char filled_until[11];

    configured_start_utc = backfill_align_to_slot(cfg->start_date_epoch_utc);
    if (day_start_utc(configured_start_utc, &configured_start_utc) != 0) {
        return 1;
    }
    if (tomorrow_day_start_utc(&end_day_utc) != 0) {
        return 1;
    }
    if (determine_fetch_start_utc(configured_start_utc, &fetch_start_utc) != 0) {
        return 1;
    }
    fetch_start_utc = max_i64(fetch_start_utc, configured_start_utc);
    if (fetch_start_utc > end_day_utc) {
        fetch_start_utc = end_day_utc;
    }

    if (backfill_epoch_to_ymd_utc(end_day_utc, filled_until) != 0) {
        backfill_copy_string_safe(filled_until, sizeof(filled_until), "unknown");
    }
    if (snprintf(
            msg,
            sizeof(msg),
            "configured_start=%" PRId64 " fetch_start=%" PRId64 " filled_until=%s",
            configured_start_utc,
            fetch_start_utc,
            filled_until) >= 0) {
        (void)SS_LOG_INFO("backfill.price.plan", msg);
    }

    rate_limiter_init(&rl);
    for (day_utc = fetch_start_utc; day_utc <= end_day_utc; day_utc += 86400) {
        day_count += 1U;
        if (fetch_and_write_day(cfg, &rl, day_utc, &records_written) != 0) {
            return 1;
        }
    }

    if (snprintf(msg, sizeof(msg), "days_fetched=%zu rows_written=%zu filled_until=%s", day_count, records_written, filled_until) >= 0) {
        (void)SS_LOG_INFO("backfill.price.complete", msg);
    }
    return 0;
}

int main(int argc, char **argv)
{
    backfill_config cfg;

    (void)argc;
    (void)argv;

    if (!backfill_has_env_config()) {
        (void)fprintf(stderr, "backfill price: missing SUNSPOTS_CONFIG or SUNSPOTS_SYSTEM\n");
        return EXIT_FAILURE;
    }

    backfill_config_parse(&cfg);
    if (strcmp(cfg.endpoint, "https://archive-api.open-meteo.com/v1/archive") == 0) {
        backfill_copy_string_safe(cfg.endpoint, sizeof(cfg.endpoint), "https://www.elprisetjustnu.se/api/v1/prices");
    }
    log_cfg(&cfg);

    if (!cfg.enabled) {
        (void)SS_LOG_INFO("backfill.price.disabled", "price backfill disabled by config");
        return EXIT_SUCCESS;
    }
    if (!cfg.has_system_location || cfg.elprisomrade[0] == '\0') {
        (void)SS_LOG_ERROR("backfill.price.invalid_location", "missing system.location elprisomrade");
        return EXIT_FAILURE;
    }
    {
        int priority_rc = backfill_apply_process_priority(&cfg);
        if (priority_rc > 0) {
            (void)SS_LOG_INFO("backfill.price.priority", "applied low process priority");
        } else if (priority_rc < 0 && cfg.process_nice > 0) {
            (void)SS_LOG_WARN("backfill.price.priority_failed", "failed to apply low process priority");
        }
    }
    return (run_price_backfill_once(&cfg) == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
