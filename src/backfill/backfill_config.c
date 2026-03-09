#include "backfill_config.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../sdk/ss_sdk.h"
#include "../sdk/internal/ss_sdk_config_util.h"

typedef struct {
    const char *key;
    int min_v;
    int *field;
} cfg_int_binding;

static int clamp_min_int(int v, int min_v, int fallback)
{
    if (v < min_v) {
        return fallback;
    }
    return v;
}

static void backfill_config_defaults(backfill_config *cfg)
{
    memset(cfg, 0, sizeof(*cfg));
    cfg->enabled = 1;
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
    backfill_copy_string_safe(cfg->endpoint, sizeof(cfg->endpoint), "https://archive-api.open-meteo.com/v1/archive");
    backfill_copy_string_safe(cfg->single_runs_endpoint, sizeof(cfg->single_runs_endpoint), BACKFILL_SINGLE_RUNS_ENDPOINT);
    backfill_copy_string_safe(cfg->usage_daily_path, sizeof(cfg->usage_daily_path), BACKFILL_USAGE_DAILY_PATH);
    backfill_copy_string_safe(cfg->start_date_utc, sizeof(cfg->start_date_utc), "2025-01-01");
    cfg->start_date_epoch_utc = 1735689600;
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
        backfill_copy_string_safe(cfg->location_name, sizeof(cfg->location_name), location.name);
    }
    if (location.elprisomrade[0] != '\0') {
        backfill_copy_string_safe(cfg->elprisomrade, sizeof(cfg->elprisomrade), location.elprisomrade);
    }
}

static int backfill_cfg_path(char *out, size_t out_sz, const char *key)
{
    int n;

    if (out == NULL || out_sz == 0U || key == NULL || key[0] == '\0') {
        return -1;
    }
    n = snprintf(out, out_sz, "backfill.%s", key);
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
    if (backfill_cfg_path(path, sizeof(path), key) == 0 &&
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
    if (backfill_cfg_path(path, sizeof(path), key) == 0 &&
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
    if (backfill_cfg_path(path, sizeof(path), key) == 0 &&
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

int backfill_has_env_config(void)
{
    const char *cfg = getenv("SUNSPOTS_CONFIG");
    const char *sys = getenv("SUNSPOTS_SYSTEM");

    if (cfg == NULL || cfg[0] == '\0') {
        return 0;
    }
    if (sys == NULL || sys[0] == '\0') {
        return 0;
    }
    return 1;
}

void backfill_config_parse(backfill_config *cfg)
{
    int value = 0;
    cfg_int_binding int_bindings[] = {
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

    if (cfg == NULL) {
        return;
    }

    backfill_config_defaults(cfg);

    if (backfill_cfg_try_get_bool("enabled", &value)) {
        cfg->enabled = value ? 1 : 0;
    }
    for (i = 0U; i < sizeof(int_bindings) / sizeof(int_bindings[0]); ++i) {
        backfill_cfg_apply_int(int_bindings[i].key, int_bindings[i].min_v, int_bindings[i].field);
    }

    if (cfg->max_requests_per_minute > BACKFILL_OPENMETEO_LIMIT_PER_MIN) {
        cfg->max_requests_per_minute = BACKFILL_OPENMETEO_LIMIT_PER_MIN;
    }
    if (cfg->max_requests_per_hour > BACKFILL_OPENMETEO_LIMIT_PER_HOUR) {
        cfg->max_requests_per_hour = BACKFILL_OPENMETEO_LIMIT_PER_HOUR;
    }
    if (cfg->max_requests_per_day > BACKFILL_OPENMETEO_LIMIT_PER_DAY) {
        cfg->max_requests_per_day = BACKFILL_OPENMETEO_LIMIT_PER_DAY;
    }

    (void)backfill_cfg_try_get_string("endpoint", cfg->endpoint, sizeof(cfg->endpoint));
    (void)backfill_cfg_try_get_string("single_runs_endpoint", cfg->single_runs_endpoint, sizeof(cfg->single_runs_endpoint));
    (void)backfill_cfg_try_get_string("usage_daily_path", cfg->usage_daily_path, sizeof(cfg->usage_daily_path));
    if (backfill_cfg_try_get_string("start_date_utc", cfg->start_date_utc, sizeof(cfg->start_date_utc))) {
        int64_t parsed_epoch = 0;
        if (backfill_parse_utc_date(cfg->start_date_utc, &parsed_epoch) == 0) {
            cfg->start_date_epoch_utc = parsed_epoch;
        } else {
            backfill_copy_string_safe(cfg->start_date_utc, sizeof(cfg->start_date_utc), "2025-01-01");
            cfg->start_date_epoch_utc = 1735689600;
            (void)SS_LOG_WARN("backfill.config.invalid_start_date", "invalid start_date_utc, falling back to 2025-01-01");
        }
    }

    backfill_config_apply_system_location(cfg);
}
