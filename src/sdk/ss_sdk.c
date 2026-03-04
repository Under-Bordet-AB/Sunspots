#include "sdk/ss_sdk.h"

#include <limits.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "sdk/internal/db/ss_db_internal.h"
#include "sdk/internal/ss_sdk_config.h"
#include "sdk/internal/log/ss_log_internal.h"

enum {
    SS_SLOT_SECONDS = 900,
    SS_MAX_QUARTERS = 672
};

static int g_ss_sdk_atexit_registered = 0;
static int g_ss_sdk_shutdown_logged = 0;

static void ss_sdk_atexit_shutdown(void);

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

static const char *ss_sdk_status_to_text(ss_sdk_status status)
{
    switch (status) {
        case SS_SDK_OK:
            return "ok";
        case SS_SDK_ERR_PARTIAL_DATA:
            return "partial_data";
        case SS_SDK_ERR_INVALID_ARG:
            return "invalid_arg";
        case SS_SDK_ERR_VALIDATION:
            return "validation";
        case SS_SDK_ERR_NOT_IMPLEMENTED:
            return "not_implemented";
        case SS_SDK_ERR_INTERNAL:
            return "internal";
        default:
            return "unknown";
    }
}

static void ss_sdk_log_status_non_ok(const char *event, ss_sdk_status status)
{
    char msg[96];
    if (event == NULL) {
        return;
    }
    if (snprintf(msg, sizeof(msg), "status=%s", ss_sdk_status_to_text(status)) < 0) {
        return;
    }
    switch (status) {
        case SS_SDK_ERR_PARTIAL_DATA:
            return;
        case SS_SDK_ERR_INVALID_ARG:
        case SS_SDK_ERR_VALIDATION:
            SS_LOG_WARN(event, msg);
            break;
        case SS_SDK_ERR_NOT_IMPLEMENTED:
        case SS_SDK_ERR_INTERNAL:
        default:
            SS_LOG_ERROR(event, msg);
            break;
    }
}

static void ss_sdk_get_process_name(char out_name[64])
{
    FILE *comm = NULL;

    if (out_name == NULL) {
        return;
    }
    (void)snprintf(out_name, 64U, "unknown");
    comm = fopen("/proc/self/comm", "r");
    if (comm != NULL) {
        if (fgets(out_name, 64U, comm) != NULL) {
            size_t n = strlen(out_name);
            while (n > 0U && (out_name[n - 1U] == '\n' || out_name[n - 1U] == '\r')) {
                out_name[n - 1U] = '\0';
                n -= 1U;
            }
        }
        fclose(comm);
    }
}

static void ss_sdk_log_started_once(void)
{
    static int started_logged = 0;
    char msg[96];
    char process_name[64];

    if (started_logged) {
        return;
    }
    started_logged = 1;

    if (!g_ss_sdk_atexit_registered) {
        if (atexit(ss_sdk_atexit_shutdown) == 0) {
            g_ss_sdk_atexit_registered = 1;
        }
    }

    ss_sdk_get_process_name(process_name);

    if (snprintf(msg, sizeof(msg), "process=%s", process_name) < 0) {
        (void)snprintf(msg, sizeof(msg), "process=unknown");
    }
    ss_sdk_internal_log_write_auto(SS_SDK_LOG_INFO, "sdk.started", msg, SS_SDK_LOG_FILE_TOKEN, __LINE__, __func__);
}

static void ss_sdk_atexit_shutdown(void)
{
    ss_sdk_shutdown();
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
    if (record->ingested_utc < 0) {
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
    out->ingested_utc = 0;
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
        ss_sdk_log_status_non_ok("sdk.api.record_make_f64.failed", status);
        return status;
    }
    if (!isfinite(value)) {
        SS_LOG_WARN("sdk.api.record_make_f64.validation_failed", "value was non-finite");
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
        ss_sdk_log_status_non_ok("sdk.api.record_make_i64.failed", status);
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
        ss_sdk_log_status_non_ok("sdk.api.record_make_bool.failed", status);
        return status;
    }
    out->value.boolean = value;
    return SS_SDK_OK;
}

ss_sdk_status ss_sdk_db_write_record(const ss_sdk_record *record)
{
    ss_sdk_record normalized;
    ss_sdk_status validation_status;

    ss_sdk_log_started_once();

    if (record == NULL) {
        SS_LOG_WARN("sdk.api.db_write.invalid_arg", "record pointer is null");
        return SS_SDK_ERR_INVALID_ARG;
    }
    if (record->ts_start_utc < 0) {
        SS_LOG_WARN("sdk.api.db_write.validation_failed", "record start time was negative");
        return SS_SDK_ERR_VALIDATION;
    }

    normalized = *record;
    normalized.ts_start_utc = ss_sdk_align_utc_to_slot(record->ts_start_utc);
    if (normalized.ts_start_utc > INT64_MAX - SS_SLOT_SECONDS) {
        SS_LOG_WARN("sdk.api.db_write.validation_failed", "aligned start time overflowed slot range");
        return SS_SDK_ERR_VALIDATION;
    }
    normalized.ts_end_utc = normalized.ts_start_utc + SS_SLOT_SECONDS;

    validation_status = ss_sdk_validate_record(&normalized);
    if (validation_status != SS_SDK_OK) {
        SS_LOG_WARN("sdk.api.db_write.validation_failed", "normalized record failed validation");
        return validation_status;
    }
    validation_status = ss_sdk_internal_db_write_record(&normalized);
    if (validation_status != SS_SDK_OK) {
        ss_sdk_log_status_non_ok("sdk.api.db_write.failed", validation_status);
        return validation_status;
    }
    return SS_SDK_OK;
}

ss_sdk_status ss_sdk_db_write_records(
    const ss_sdk_record *records,
    size_t count,
    size_t *out_written)
{
    ss_sdk_record *normalized_records = NULL;
    size_t i;
    size_t written = 0U;
    ss_sdk_status status = SS_SDK_ERR_INTERNAL;

    ss_sdk_log_started_once();

    if (out_written == NULL) {
        SS_LOG_WARN("sdk.api.db_write_batch.invalid_arg", "out_written pointer is null");
        return SS_SDK_ERR_INVALID_ARG;
    }
    *out_written = 0U;

    if (records == NULL || count == 0U) {
        SS_LOG_WARN("sdk.api.db_write_batch.invalid_arg", "records was null or count was zero");
        return SS_SDK_ERR_INVALID_ARG;
    }

    normalized_records = (ss_sdk_record *)calloc(count, sizeof(*normalized_records));
    if (normalized_records == NULL) {
        SS_LOG_ERROR("sdk.api.db_write_batch.alloc_failed", "failed to allocate normalized records");
        return SS_SDK_ERR_INTERNAL;
    }

    for (i = 0U; i < count; ++i) {
        normalized_records[i] = records[i];
        if (normalized_records[i].ts_start_utc < 0) {
            SS_LOG_WARN("sdk.api.db_write_batch.validation_failed", "record start time was negative");
            free(normalized_records);
            return SS_SDK_ERR_VALIDATION;
        }
        normalized_records[i].ts_start_utc = ss_sdk_align_utc_to_slot(normalized_records[i].ts_start_utc);
        if (normalized_records[i].ts_start_utc > INT64_MAX - SS_SLOT_SECONDS) {
            SS_LOG_WARN("sdk.api.db_write_batch.validation_failed", "aligned start time overflowed slot range");
            free(normalized_records);
            return SS_SDK_ERR_VALIDATION;
        }
        normalized_records[i].ts_end_utc = normalized_records[i].ts_start_utc + SS_SLOT_SECONDS;
        status = ss_sdk_validate_record(&normalized_records[i]);
        if (status != SS_SDK_OK) {
            ss_sdk_log_status_non_ok("sdk.api.db_write_batch.validation_failed", status);
            free(normalized_records);
            return status;
        }
    }

    status = ss_sdk_internal_db_write_records(normalized_records, count, &written);
    free(normalized_records);
    if (status != SS_SDK_OK) {
        ss_sdk_log_status_non_ok("sdk.api.db_write_batch.failed", status);
        return status;
    }
    *out_written = written;
    return SS_SDK_OK;
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

    ss_sdk_log_started_once();

    if (out == NULL) {
        SS_LOG_WARN("sdk.api.db_get.invalid_arg", "output pointer is null");
        return SS_SDK_ERR_INVALID_ARG;
    }

    out->samples = NULL;
    out->count = 0;

    // Do we have a valid canonical?
    if (ss_metric_meta_get(canonical) == NULL) {
        SS_LOG_WARN("sdk.api.db_get.invalid_arg", "canonical id was invalid");
        return SS_SDK_ERR_INVALID_ARG;
    }

    if (from_utc < 0) {
        SS_LOG_WARN("sdk.api.db_get.invalid_arg", "from_utc was negative");
        return SS_SDK_ERR_INVALID_ARG;
    }

    if (quarters_to_fetch > SS_MAX_QUARTERS) {
        SS_LOG_WARN("sdk.api.db_get.invalid_arg", "quarters_to_fetch exceeded maximum");
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
        ss_sdk_status status = ss_sdk_internal_db_get_canonical_forward(canonical, start_utc, out);
        if (status != SS_SDK_OK) {
            ss_sdk_log_status_non_ok("sdk.api.db_get.forward_failed", status);
            return status;
        }
        return SS_SDK_OK;
    }

    span = (int64_t)quarters_to_fetch * SS_SLOT_SECONDS;
    if (start_utc > INT64_MAX - span) {
        SS_LOG_WARN("sdk.api.db_get.invalid_arg", "time window overflow detected");
        return SS_SDK_ERR_INVALID_ARG;
    }
    end_utc = start_utc + span;
    {
        ss_sdk_status status = ss_sdk_internal_db_get_canonical(canonical, start_utc, end_utc, out);
        if (status != SS_SDK_OK) {
            ss_sdk_log_status_non_ok("sdk.api.db_get.range_failed", status);
            return status;
        }
        return SS_SDK_OK;
    }
}

void ss_sdk_db_free_samples(ss_sdk_samples_out *out)
{
    ss_sdk_internal_db_free_samples(out);
}

void ss_sdk_log_write_auto(
    ss_sdk_log_level level,
    const char *event,
    const char *message,
    const char *file,
    int line,
    const char *func)
{
    ss_sdk_internal_log_write_auto(level, event, message, file, line, func);
}

void ss_sdk_log_write_fields(
    ss_sdk_log_level level,
    const char *event,
    const char *message,
    const ss_sdk_log_fields *fields,
    const char *file,
    int line,
    const char *func)
{
    ss_sdk_internal_log_write_fields(level, event, message, fields, file, line, func);
}

void ss_sdk_shutdown(void)
{
    char process_name[64];
    char msg[96];

    if (!g_ss_sdk_shutdown_logged) {
        g_ss_sdk_shutdown_logged = 1;
        ss_sdk_get_process_name(process_name);
        if (snprintf(msg, sizeof(msg), "process=%s", process_name) < 0) {
            (void)snprintf(msg, sizeof(msg), "process=unknown");
        }
        ss_sdk_internal_log_write_auto(SS_SDK_LOG_INFO, "sdk.shutdown", msg, SS_SDK_LOG_FILE_TOKEN, __LINE__, __func__);
    }

    ss_sdk_internal_db_shutdown();
    ss_sdk_internal_log_shutdown();
#ifdef SS_SDK_ENABLE_TEST_HOOKS
    g_ss_sdk_now_override_enabled = 0;
    g_ss_sdk_now_override_value = 0;
#endif
}
