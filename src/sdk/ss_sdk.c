#include "sdk/ss_sdk.h"

#include <limits.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#include "sdk/internal/db/ss_db_internal.h"
#include "sdk/internal/log/ss_log_internal.h"

enum {
    SS_SLOT_SECONDS = 900,
    SS_MAX_QUARTERS = 672
};

#ifdef SS_SDK_ENABLE_TEST_HOOKS
static int g_ss_sdk_now_override_enabled = 0;
static int64_t g_ss_sdk_now_override_value = 0;

void ss_sdk_test_set_now_override(int enabled, int64_t now_value)
{
    g_ss_sdk_now_override_enabled = (enabled != 0);
    g_ss_sdk_now_override_value = now_value;
}
#endif

static bool ss_sdk_is_valid_value_type(ss_sdk_value_type value_type)
{
    return value_type == SS_SDK_VALUE_I64 ||
           value_type == SS_SDK_VALUE_F64 ||
           value_type == SS_SDK_VALUE_BOOL;
}

static bool ss_sdk_is_valid_data_kind(ss_sdk_data_kind data_kind)
{
    return data_kind == SS_SDK_DATA_OBSERVATION ||
           data_kind == SS_SDK_DATA_FORECAST;
}

static ss_sdk_status ss_sdk_validate_record(const ss_sdk_record *record)
{
    const ss_metric_meta *metric_metadata;

    if (record == NULL) {
        return SS_SDK_ERR_INVALID_ARG;
    }

    if (!ss_sdk_is_valid_value_type(record->value_type) ||
        !ss_sdk_is_valid_data_kind(record->data_kind)) {
        return SS_SDK_ERR_VALIDATION;
    }

    if (record->ts_start_utc < 0 || record->ts_start_utc % SS_SLOT_SECONDS != 0) {
        return SS_SDK_ERR_VALIDATION;
    }
    if (record->ts_start_utc > INT64_MAX - SS_SLOT_SECONDS) {
        return SS_SDK_ERR_VALIDATION;
    }
    if (record->ts_end_utc != record->ts_start_utc + SS_SLOT_SECONDS) {
        return SS_SDK_ERR_VALIDATION;
    }

    if (record->value_type == SS_SDK_VALUE_F64 && !isfinite(record->value.f64)) {
        return SS_SDK_ERR_VALIDATION;
    }

    // validate that canonical type enum exists
    metric_metadata = ss_metric_meta_get(record->metric);
    if (metric_metadata == NULL) {
        return SS_SDK_ERR_VALIDATION;
    }
    if (record->value_type != metric_metadata->value_type) {
        return SS_SDK_ERR_VALIDATION;
    }

    return SS_SDK_OK;
}

static int64_t ss_sdk_now_utc(void)
{
    int64_t now_utc;

#ifdef SS_SDK_ENABLE_TEST_HOOKS
    if (g_ss_sdk_now_override_enabled) {
        now_utc = g_ss_sdk_now_override_value;
    } else {
        now_utc = (int64_t)time(NULL);
    }
#else
    now_utc = (int64_t)time(NULL);
#endif

    if (now_utc < 0) {
        return 0;
    }
    return now_utc;
}

static int64_t ss_sdk_align_utc_to_slot(int64_t ts_utc)
{
    if (ts_utc < 0) {
        return 0;
    }
    return ts_utc - (ts_utc % SS_SLOT_SECONDS);
}

static ss_sdk_status ss_sdk_record_make_common(
    ss_sdk_record *out,
    ss_metric_id metric,
    ss_sdk_value_type expected_type,
    int64_t ts_utc,
    ss_sdk_data_kind data_kind)
{
    const ss_metric_meta *metric_metadata;
    int64_t start_utc;

    if (out == NULL || ts_utc < 0) {
        return SS_SDK_ERR_INVALID_ARG;
    }
    if (!ss_sdk_is_valid_data_kind(data_kind)) {
        return SS_SDK_ERR_VALIDATION;
    }

    metric_metadata = ss_metric_meta_get(metric);
    if (metric_metadata == NULL) {
        return SS_SDK_ERR_VALIDATION;
    }
    if (metric_metadata->value_type != expected_type) {
        return SS_SDK_ERR_VALIDATION;
    }

    start_utc = ss_sdk_align_utc_to_slot(ts_utc);
    if (start_utc > INT64_MAX - SS_SLOT_SECONDS) {
        return SS_SDK_ERR_INVALID_ARG;
    }

    out->metric = metric;
    out->value_type = expected_type;
    out->ts_start_utc = start_utc;
    out->ts_end_utc = start_utc + SS_SLOT_SECONDS;
    out->data_kind = data_kind;
    return SS_SDK_OK;
}

ss_sdk_status ss_sdk_record_make_f64(
    ss_sdk_record *out,
    ss_metric_id metric,
    double value,
    int64_t ts_utc,
    ss_sdk_data_kind data_kind)
{
    ss_sdk_status status = ss_sdk_record_make_common(out, metric, SS_SDK_VALUE_F64, ts_utc, data_kind);
    if (status != SS_SDK_OK) {
        return status;
    }
    if (!isfinite(value)) {
        return SS_SDK_ERR_VALIDATION;
    }
    out->value.f64 = value;
    return SS_SDK_OK;
}

ss_sdk_status ss_sdk_record_make_i64(
    ss_sdk_record *out,
    ss_metric_id metric,
    int64_t value,
    int64_t ts_utc,
    ss_sdk_data_kind data_kind)
{
    ss_sdk_status status = ss_sdk_record_make_common(out, metric, SS_SDK_VALUE_I64, ts_utc, data_kind);
    if (status != SS_SDK_OK) {
        return status;
    }
    out->value.i64 = value;
    return SS_SDK_OK;
}

ss_sdk_status ss_sdk_record_make_bool(
    ss_sdk_record *out,
    ss_metric_id metric,
    bool value,
    int64_t ts_utc,
    ss_sdk_data_kind data_kind)
{
    ss_sdk_status status = ss_sdk_record_make_common(out, metric, SS_SDK_VALUE_BOOL, ts_utc, data_kind);
    if (status != SS_SDK_OK) {
        return status;
    }
    out->value.boolean = value;
    return SS_SDK_OK;
}

ss_sdk_status ss_sdk_db_write_record(const ss_sdk_record *record)
{
    ss_sdk_record normalized;
    ss_sdk_status validation_status;

    if (record == NULL) {
        return SS_SDK_ERR_INVALID_ARG;
    }
    if (record->ts_start_utc < 0) {
        return SS_SDK_ERR_VALIDATION;
    }

    normalized = *record;
    normalized.ts_start_utc = ss_sdk_align_utc_to_slot(record->ts_start_utc);
    if (normalized.ts_start_utc > INT64_MAX - SS_SLOT_SECONDS) {
        return SS_SDK_ERR_VALIDATION;
    }
    normalized.ts_end_utc = normalized.ts_start_utc + SS_SLOT_SECONDS;

    validation_status = ss_sdk_validate_record(&normalized);
    if (validation_status != SS_SDK_OK) {
        return validation_status;
    }
    return ss_sdk_internal_db_write_record(&normalized);
}

ss_sdk_status ss_sdk_db_get_canonical(
    int64_t from_utc,
    uint16_t quarters_to_fetch,
    ss_metric_id canonical,
    ss_sdk_samples_out *out)
{
    int64_t start_utc;
    int64_t span;
    int64_t end_utc;

    if (out == NULL) {
        return SS_SDK_ERR_INVALID_ARG;
    }

    out->samples = NULL;
    out->count = 0;

    // Do we have a valid canonical?
    if (ss_metric_meta_get(canonical) == NULL) {
        return SS_SDK_ERR_INVALID_ARG;
    }

    if (from_utc < 0) {
        return SS_SDK_ERR_INVALID_ARG;
    }

    if (quarters_to_fetch > SS_MAX_QUARTERS) {
        return SS_SDK_ERR_INVALID_ARG;
    }

    // Align start time to SS_SLOT_SECONDS
    if (from_utc == 0) {
        start_utc = ss_sdk_align_utc_to_slot(ss_sdk_now_utc());
    } else {
        start_utc = ss_sdk_align_utc_to_slot(from_utc);
    }

    // 0 == fetch current and all forcast
    if (quarters_to_fetch == 0) {
        return ss_sdk_internal_db_get_canonical_forward(canonical, start_utc, out);
    }

    span = (int64_t)quarters_to_fetch * SS_SLOT_SECONDS;
    if (start_utc > INT64_MAX - span) {
        return SS_SDK_ERR_INVALID_ARG;
    }
    end_utc = start_utc + span;

    return ss_sdk_internal_db_get_canonical(canonical, start_utc, end_utc, out);
}

void ss_sdk_db_free_samples(ss_sdk_samples_out *out)
{
    ss_sdk_internal_db_free_samples(out);
}

ss_sdk_status ss_sdk_log_write_auto(
    ss_sdk_log_level level,
    const char *event,
    const char *message,
    const char *file,
    int line,
    const char *func)
{
    return ss_sdk_internal_log_write_auto(level, event, message, file, line, func);
}

ss_sdk_status ss_sdk_log_write_fields(
    ss_sdk_log_level level,
    const char *event,
    const char *message,
    const ss_sdk_log_fields *fields,
    const char *file,
    int line,
    const char *func)
{
    return ss_sdk_internal_log_write_fields(level, event, message, fields, file, line, func);
}

void ss_sdk_shutdown(void)
{
    ss_sdk_internal_db_shutdown();
    ss_sdk_internal_log_shutdown();
#ifdef SS_SDK_ENABLE_TEST_HOOKS
    g_ss_sdk_now_override_enabled = 0;
    g_ss_sdk_now_override_value = 0;
#endif
}
