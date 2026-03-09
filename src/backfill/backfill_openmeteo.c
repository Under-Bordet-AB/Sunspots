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
#include <sys/stat.h>
#include <unistd.h>

#include "../fetch/fetch_utils.h"
#include "../sdk/ss_sdk.h"
#include "backfill_common.h"
#include "backfill_config.h"
#include "backfill_holes.h"
#include "backfill_payload.h"

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

struct backfill_dashboard;

typedef struct {
    rate_limiter rl;
    size_t records_written;
    struct backfill_dashboard *dashboard;
} forecast_history_ctx;

static void backfill_forecast_history_reset_dashboard(struct backfill_dashboard *dashboard);
static void backfill_forecast_history_configure_dashboard(struct backfill_dashboard *dashboard, int64_t start_utc, int64_t end_utc);
static void backfill_forecast_history_mark_done(forecast_history_ctx *ctx, const backfill_config *cfg);
static int backfill_forecast_history_fetch_and_write(forecast_history_ctx *ctx, const backfill_config *cfg, int64_t run_utc);
static int backfill_forecast_history_run_once(forecast_history_ctx *ctx, const backfill_config *cfg, int64_t run_utc);

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

static void log_cfg(const backfill_config *cfg)
{
    char msg[384];
    if (snprintf(msg,
                 sizeof(msg),
                 "enabled=%d location=%s area=%s start_date=%s chunk_days=%d retries=%d backoff_ms=%d lag_min=%d req_interval_ms=%d limit_m=%d limit_h=%d limit_d=%d lat=%.4f lon=%.4f endpoint=%.80s single_runs=%.80s",
                 cfg->enabled,
                 cfg->location_name,
                 cfg->elprisomrade,
                 cfg->start_date_utc,
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
                 cfg->endpoint,
                 cfg->single_runs_endpoint) < 0) {
        backfill_copy_string_safe(msg, sizeof(msg), "config logging failed");
    }
    (void)SS_LOG_INFO("backfill.config", msg);
    if (snprintf(msg, sizeof(msg), "usage_daily_path=%.120s", cfg->usage_daily_path) >= 0) {
        (void)SS_LOG_INFO("backfill.usage.path", msg);
    }
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
            usage_bump_daily_count(cfg->usage_daily_path, 1);
            return;
        }

        wait_sec = compute_wait_seconds(nowv, hit_minute, hit_hour, hit_day);
        if (snprintf(msg, sizeof(msg), "throttling wait=%ds", wait_sec) < 0) {
            backfill_copy_string_safe(msg, sizeof(msg), "throttling wait");
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
            backfill_copy_string_safe(msg, sizeof(msg), "fetch retry");
        }
        (void)SS_LOG_WARN("backfill.fetch_retry", msg);
        sleep_ms(wait_ms);
        rc = -1;
    }

    return rc;
}

typedef struct {
    const backfill_config *cfg;
    rate_limiter *rl;
    size_t hole_index;
    size_t hole_count;
    size_t *records_written;
    time_t *last_progress;
    struct backfill_dashboard *dashboard;
} backfill_hole_ctx;

typedef struct backfill_dashboard {
    time_t started_at;
    time_t last_render;
    int is_tty;
    char phase[64];
    size_t holes_found;
    size_t hole_index;
    size_t hole_count;
    size_t chunks_done;
    size_t chunks_total;
    size_t records_written;
    size_t forecast_runs_done;
    size_t forecast_runs_total;
} backfill_dashboard;

static size_t backfill_count_forecast_runs(int64_t start_utc, int64_t end_utc)
{
    int64_t run_utc;
    size_t count = 0U;

    if (start_utc >= end_utc) {
        return 0U;
    }
    run_utc = start_utc - (start_utc % BACKFILL_FORECAST_RUN_STEP_SECONDS);
    while (run_utc < end_utc) {
        count += 1U;
        run_utc += BACKFILL_FORECAST_RUN_STEP_SECONDS;
    }
    return count;
}

static void backfill_dashboard_render(backfill_dashboard *dash, int force)
{
    char line[512];
    time_t now_ts;
    int elapsed_sec;
    int n;

    if (dash == NULL) {
        return;
    }

    now_ts = time(NULL);
    if (!force && (int)(now_ts - dash->last_render) < 1) {
        return;
    }

    elapsed_sec = (int)(now_ts - dash->started_at);
    n = snprintf(
        line,
        sizeof(line),
        "Backfill | phase=%s | holes=%zu | hole=%zu/%zu | chunks=%zu/%zu | forecast=%zu/%zu | records=%zu | elapsed=%dm%02ds",
        dash->phase,
        dash->holes_found,
        dash->hole_index,
        dash->hole_count,
        dash->chunks_done,
        dash->chunks_total,
        dash->forecast_runs_done,
        dash->forecast_runs_total,
        dash->records_written,
        elapsed_sec / 60,
        elapsed_sec % 60);
    if (n < 0) {
        return;
    }

    if (dash->is_tty) {
        (void)fprintf(stdout, "\r%-150s", line);
        (void)fflush(stdout);
    } else {
        (void)fprintf(stdout, "%s\n", line);
        (void)fflush(stdout);
    }

    dash->last_render = now_ts;
}

static void backfill_dashboard_set_phase(backfill_dashboard *dash, const char *phase)
{
    if (dash == NULL) {
        return;
    }
    backfill_copy_string_safe(dash->phase, sizeof(dash->phase), phase);
    backfill_dashboard_render(dash, 1);
}

static void backfill_dashboard_init(backfill_dashboard *dash)
{
    if (dash == NULL) {
        return;
    }
    memset(dash, 0, sizeof(*dash));
    dash->started_at = time(NULL);
    dash->last_render = 0;
    dash->is_tty = isatty(STDOUT_FILENO) ? 1 : 0;
    backfill_copy_string_safe(dash->phase, sizeof(dash->phase), "starting");
    if (dash->is_tty) {
        (void)fprintf(stdout, "Backfill dashboard started\n");
    }
    backfill_dashboard_render(dash, 1);
}

static void backfill_dashboard_finish(backfill_dashboard *dash, int ok)
{
    if (dash == NULL) {
        return;
    }
    if (ok) {
        backfill_dashboard_set_phase(dash, "complete");
    } else {
        backfill_dashboard_set_phase(dash, "failed");
    }
    if (dash->is_tty) {
        (void)fprintf(stdout, "\n");
    }
}

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
        backfill_copy_string_safe(msg, sizeof(msg), "backfill progress");
    }
        (void)SS_LOG_DEBUG("backfill.progress", msg);
    *(ctx->last_progress) = now_ts;
}

static int backfill_process_chunk(
    backfill_hole_ctx *ctx,
    int64_t part_start,
    int64_t next_end)
{
    char url[BACKFILL_MAX_URL_LEN];
    char *buf = NULL;
    size_t wrote_now = 0U;
    char msg[256];

    if (ctx == NULL || ctx->cfg == NULL || ctx->rl == NULL || ctx->records_written == NULL) {
        return -1;
    }
    if (backfill_build_archive_url(url, ctx->cfg, part_start, next_end) != 0) {
        return -1;
    }

    if (snprintf(
            msg,
            sizeof(msg),
            "hole=%zu/%zu chunk_start=%" PRId64 " chunk_end=%" PRId64,
            ctx->hole_index,
            ctx->hole_count,
            part_start,
            next_end) >= 0) {
        (void)SS_LOG_DEBUG("backfill.chunk_fetch_start", msg);
    }

    rate_limiter_maybe_wait(ctx->rl, ctx->cfg);
    if (fetch_json_with_retry(url, ctx->cfg->retry_max_attempts, ctx->cfg->retry_base_backoff_ms, &buf) != 0) {
        return -1;
    }

    if (backfill_write_archive_payload(buf, part_start, next_end, &wrote_now) != 0) {
        free(buf);
        return -1;
    }
    free(buf);
    buf = NULL;

    *(ctx->records_written) += wrote_now;
    if (snprintf(
            msg,
            sizeof(msg),
            "hole=%zu/%zu chunk_start=%" PRId64 " chunk_end=%" PRId64 " rows_written=%zu total_rows=%zu",
            ctx->hole_index,
            ctx->hole_count,
            part_start,
            next_end,
            wrote_now,
            *(ctx->records_written)) >= 0) {
        (void)SS_LOG_DEBUG("backfill.chunk_fetch_complete", msg);
    }
    if (ctx->dashboard != NULL) {
        ctx->dashboard->chunks_done += 1U;
        ctx->dashboard->records_written = *(ctx->records_written);
        backfill_dashboard_render(ctx->dashboard, 0);
    }
    sleep_ms(ctx->cfg->request_interval_ms);
    return 0;
}

static int backfill_process_hole(backfill_hole_ctx *ctx, const hole_range *hole)
{
    int64_t chunk_seconds;
    int64_t part_start;
    int64_t part_end;
    int64_t span_seconds;
    int64_t chunk_count;
    char msg[256];

    if (ctx == NULL || ctx->cfg == NULL || hole == NULL) {
        return -1;
    }

    chunk_seconds = (int64_t)ctx->cfg->chunk_days * 86400;
    part_start = hole->from_utc;
    part_end = hole->to_utc;
    span_seconds = part_end - part_start;
    chunk_count = (chunk_seconds > 0) ? ((span_seconds + chunk_seconds - 1) / chunk_seconds) : 0;

    if (snprintf(
            msg,
            sizeof(msg),
            "hole=%zu/%zu from=%" PRId64 " to=%" PRId64 " span_sec=%" PRId64 " chunks=%" PRId64,
            ctx->hole_index,
            ctx->hole_count,
            part_start,
            part_end,
            span_seconds,
            chunk_count) >= 0) {
        (void)SS_LOG_DEBUG("backfill.hole_fill_start", msg);
    }

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

    if (snprintf(
            msg,
            sizeof(msg),
            "hole=%zu/%zu completed total_rows=%zu",
            ctx->hole_index,
            ctx->hole_count,
            *(ctx->records_written)) >= 0) {
        (void)SS_LOG_DEBUG("backfill.hole_fill_complete", msg);
    }

    return 0;
}

static int backfill_holes(const backfill_config *cfg, const hole_list *holes, size_t *out_records_written, backfill_dashboard *dashboard)
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
    ctx.dashboard = dashboard;

    if (dashboard != NULL) {
        dashboard->hole_count = holes->count;
        dashboard->chunks_total = backfill_count_total_chunks(holes, cfg->chunk_days);
        dashboard->chunks_done = 0U;
        backfill_dashboard_set_phase(dashboard, "filling holes");
    }
    {
        char msg[160];
        if (snprintf(
                msg,
                sizeof(msg),
                "holes=%zu chunks_total=%zu chunk_days=%d",
                holes->count,
                backfill_count_total_chunks(holes, cfg->chunk_days),
                cfg->chunk_days) >= 0) {
            (void)SS_LOG_DEBUG("backfill.hole_fill_plan", msg);
        }
    }

    for (i = 0U; i < holes->count; ++i) {
        ctx.hole_index = i + 1U;
        if (ctx.dashboard != NULL) {
            ctx.dashboard->hole_index = ctx.hole_index;
            ctx.dashboard->hole_count = ctx.hole_count;
            backfill_dashboard_render(ctx.dashboard, 0);
        }
        if (backfill_process_hole(&ctx, &holes->items[i]) != 0) {
            return -1;
        }
    }

    if (out_records_written != NULL) {
        *out_records_written = records_written;
    }
    {
        char msg[160];
        if (snprintf(msg, sizeof(msg), "holes=%zu total_rows=%zu", holes->count, records_written) >= 0) {
            (void)SS_LOG_DEBUG("backfill.hole_fill_done", msg);
        }
    }
    return 0;
}

static int backfill_forecast_history_window(
    const backfill_config *cfg,
    int64_t start_utc,
    int64_t end_utc,
    size_t *out_records_written,
    backfill_dashboard *dashboard)
{
    int64_t run_utc;
    forecast_history_ctx ctx;

    if (cfg == NULL || start_utc >= end_utc) {
        return -1;
    }
    if (strncmp(cfg->endpoint, "file://", 7) == 0) {
        if (out_records_written != NULL) {
            *out_records_written = 0U;
        }
        backfill_forecast_history_reset_dashboard(dashboard);
        return 0;
    }

    memset(&ctx, 0, sizeof(ctx));
    ctx.rl.minute_epoch = -1;
    ctx.rl.hour_epoch = -1;
    ctx.rl.day_epoch = -1;
    ctx.dashboard = dashboard;
    backfill_forecast_history_configure_dashboard(dashboard, start_utc, end_utc);

    run_utc = start_utc - (start_utc % BACKFILL_FORECAST_RUN_STEP_SECONDS);
    while (run_utc < end_utc) {
        if (backfill_forecast_history_run_once(&ctx, cfg, run_utc) != 0) {
            return -1;
        }
        run_utc += BACKFILL_FORECAST_RUN_STEP_SECONDS;
    }

    if (out_records_written != NULL) {
        *out_records_written = ctx.records_written;
    }
    return 0;
}

static void backfill_forecast_history_reset_dashboard(backfill_dashboard *dashboard)
{
    if (dashboard == NULL) {
        return;
    }
    dashboard->forecast_runs_total = 0U;
    dashboard->forecast_runs_done = 0U;
}

static void backfill_forecast_history_configure_dashboard(backfill_dashboard *dashboard, int64_t start_utc, int64_t end_utc)
{
    if (dashboard == NULL) {
        return;
    }
    dashboard->forecast_runs_total = backfill_count_forecast_runs(start_utc, end_utc);
    dashboard->forecast_runs_done = 0U;
    backfill_dashboard_set_phase(dashboard, "filling forecast history");
}

static void backfill_forecast_history_mark_done(forecast_history_ctx *ctx, const backfill_config *cfg)
{
    if (ctx == NULL) {
        return;
    }
    if (ctx->dashboard != NULL) {
        ctx->dashboard->forecast_runs_done += 1U;
        backfill_dashboard_render(ctx->dashboard, 0);
    }
    sleep_ms(cfg->request_interval_ms);
}

static int backfill_forecast_history_fetch_and_write(forecast_history_ctx *ctx, const backfill_config *cfg, int64_t run_utc)
{
    char run_url[BACKFILL_MAX_URL_LEN];
    char *buf = NULL;
    size_t wrote_now = 0U;

    if (ctx == NULL || cfg == NULL) {
        return -1;
    }
    if (backfill_build_single_run_url(run_url, cfg, run_utc) != 0) {
        return -1;
    }
    rate_limiter_maybe_wait(&ctx->rl, cfg);
    if (fetch_json_with_retry(run_url, cfg->retry_max_attempts, cfg->retry_base_backoff_ms, &buf) == 0 && buf != NULL) {
        if (backfill_write_single_run_payload(buf, run_utc, &wrote_now) == 0) {
            ctx->records_written += wrote_now;
            if (ctx->dashboard != NULL) {
                ctx->dashboard->records_written += wrote_now;
            }
        } else {
            (void)SS_LOG_WARN("backfill.forecast_parse_failed", "single-run payload parse/write failed");
        }
        free(buf);
        return 0;
    }

    (void)SS_LOG_WARN("backfill.forecast_fetch_failed", "single-run fetch failed");
    return 0;
}

static int backfill_forecast_history_run_once(forecast_history_ctx *ctx, const backfill_config *cfg, int64_t run_utc)
{
    if (ctx == NULL || cfg == NULL) {
        return -1;
    }
    if (backfill_forecast_history_fetch_and_write(ctx, cfg, run_utc) != 0) {
        return -1;
    }
    backfill_forecast_history_mark_done(ctx, cfg);
    return 0;
}

static int backfill_analyze_window(int64_t start_utc, int64_t end_utc, hole_list *before, backfill_dashboard *dashboard)
{
    char end_date[11];
    char msg[256];

    if (backfill_detect_all_holes(start_utc, end_utc, before) != 0) {
        (void)SS_LOG_ERROR("backfill.detect_failed", "failed to analyze current DB holes");
        return -1;
    }
    if (backfill_epoch_to_ymd_utc(end_utc > 0 ? end_utc - 1 : end_utc, end_date) != 0) {
        backfill_copy_string_safe(end_date, sizeof(end_date), "unknown");
    }
    if (snprintf(
            msg,
            sizeof(msg),
            "window_start=%" PRId64 " window_end=%" PRId64 " filled_until=%s holes_before=%zu",
            start_utc,
            end_utc,
            end_date,
            before->count) < 0) {
        backfill_copy_string_safe(msg, sizeof(msg), "backfill analyze");
    }
    (void)SS_LOG_INFO("backfill.analyze", msg);
    if (before->count == 0U) {
        if (snprintf(msg, sizeof(msg), "database has no holes and is filled until %s", end_date) >= 0) {
            (void)SS_LOG_INFO("backfill.no_holes", msg);
        }
    }
    if (dashboard != NULL) {
        dashboard->holes_found = before->count;
        dashboard->hole_count = before->count;
        backfill_dashboard_render(dashboard, 1);
    }
    return 0;
}

static int backfill_verify_window(int64_t start_utc, int64_t end_utc, size_t records_written, hole_list *after, backfill_dashboard *dashboard)
{
    char end_date[11];
    char msg[256];

    if (backfill_detect_all_holes(start_utc, end_utc, after) != 0) {
        (void)SS_LOG_ERROR("backfill.verify_failed", "failed to verify DB completeness");
        return -1;
    }
    if (backfill_epoch_to_ymd_utc(end_utc > 0 ? end_utc - 1 : end_utc, end_date) != 0) {
        backfill_copy_string_safe(end_date, sizeof(end_date), "unknown");
    }
    if (snprintf(msg, sizeof(msg), "records_written=%zu holes_after=%zu filled_until=%s", records_written, after->count, end_date) < 0) {
        backfill_copy_string_safe(msg, sizeof(msg), "backfill verify");
    }
    if (dashboard != NULL) {
        dashboard->records_written = records_written;
        backfill_dashboard_set_phase(dashboard, "verifying");
        backfill_dashboard_render(dashboard, 1);
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
    int64_t end_utc = backfill_align_to_slot(now_utc() - ((int64_t)cfg->freshness_lag_minutes * 60));
    int64_t start_utc = backfill_align_to_slot(cfg->start_date_epoch_utc);
    hole_list before = {0};
    hole_list after = {0};
    size_t records_written_obs = 0U;
    size_t records_written_fc = 0U;
    backfill_dashboard dashboard;

    backfill_dashboard_init(&dashboard);
    backfill_dashboard_set_phase(&dashboard, "analyzing holes");

    if (start_utc >= end_utc) {
        (void)SS_LOG_WARN("backfill.window_invalid", "computed window invalid");
        backfill_dashboard_finish(&dashboard, 0);
        return 1;
    }

    if (backfill_analyze_window(start_utc, end_utc, &before, &dashboard) != 0) {
        backfill_hole_list_free(&before);
        backfill_dashboard_finish(&dashboard, 0);
        return 1;
    }

    if (before.count > 0U) {
        if (backfill_holes(cfg, &before, &records_written_obs, &dashboard) != 0) {
            (void)SS_LOG_ERROR("backfill.fill_failed", "fetch/write loop failed");
            backfill_hole_list_free(&before);
            backfill_dashboard_finish(&dashboard, 0);
            return 1;
        }
    }
    dashboard.records_written = records_written_obs;
    backfill_dashboard_render(&dashboard, 1);
    backfill_hole_list_free(&before);

    if (backfill_forecast_history_window(cfg, start_utc, end_utc, &records_written_fc, &dashboard) != 0) {
        (void)SS_LOG_WARN("backfill.forecast_fill_failed", "fetch/write forecast history loop failed");
        records_written_fc = 0U;
    }
    dashboard.records_written = records_written_obs + records_written_fc;
    backfill_dashboard_render(&dashboard, 1);

    {
        int verify_rc = backfill_verify_window(start_utc, end_utc, records_written_obs + records_written_fc, &after, &dashboard);
        if (verify_rc < 0) {
            backfill_hole_list_free(&after);
            backfill_dashboard_finish(&dashboard, 0);
            return 1;
        }
        if (verify_rc == 0) {
            backfill_hole_list_free(&after);
            backfill_dashboard_finish(&dashboard, 1);
            return 0;
        }
    }
    backfill_hole_list_free(&after);
    backfill_dashboard_finish(&dashboard, 0);
    return 1;
}

int main(int argc, char **argv)
{
    backfill_config cfg;
    (void)argc;
    (void)argv;

    if (!backfill_has_env_config()) {
        (void)fprintf(stderr, "backfill: missing SUNSPOTS_CONFIG or SUNSPOTS_SYSTEM\n");
        return EXIT_FAILURE;
    }

    backfill_config_parse(&cfg);
    log_cfg(&cfg);

    if (!cfg.enabled) {
        (void)SS_LOG_INFO("backfill.disabled", "backfill disabled by config");
        return EXIT_SUCCESS;
    }

    if (!cfg.has_system_location) {
        (void)SS_LOG_ERROR("backfill.config.invalid_location", "missing system.location latitude/longitude");
        return EXIT_FAILURE;
    }

    if (run_backfill_once(&cfg) != 0) {
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
