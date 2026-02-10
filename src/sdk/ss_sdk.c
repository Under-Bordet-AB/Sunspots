#include "sdk/ss_sdk.h"

#include <string.h>

#include "sdk/internal/db/ss_db_internal.h"
#include "sdk/internal/log/ss_log_internal.h"

static bool ss_sdk_is_valid_value_type(ss_sdk_value_type value_type)
{
    static const unsigned char k_valid_value_types[] = {
        [SS_SDK_VALUE_I64] = 1U,
        [SS_SDK_VALUE_F64] = 1U,
        [SS_SDK_VALUE_STR] = 1U,
        [SS_SDK_VALUE_BOOL] = 1U,
    };
    const unsigned int idx = (unsigned int)value_type;
    const size_t value_type_count = sizeof(k_valid_value_types) / sizeof(k_valid_value_types[0]);
    const bool in_range = idx < value_type_count;
    const bool is_valid = in_range && k_valid_value_types[idx] != 0U;

    return is_valid;
}

static bool ss_sdk_is_nonempty(const char *s)
{
    return s != NULL && s[0] != '\0';
}

static ss_sdk_status ss_sdk_validate_record(const ss_sdk_record *rec)
{
    const ss_metric_meta *meta;

    if (rec == NULL) {
        return SS_SDK_ERR_INVALID_ARG;
    }

    if (!ss_sdk_is_valid_value_type(rec->value_type)) {
        return SS_SDK_ERR_VALIDATION;
    }
    if (rec->ts_start_utc <= 0 || rec->ts_end_utc <= 0) {
        return SS_SDK_ERR_VALIDATION;
    }
    if (rec->ts_end_utc < rec->ts_start_utc) {
        return SS_SDK_ERR_VALIDATION;
    }
    if (!ss_sdk_is_nonempty(rec->source_api) || !ss_sdk_is_nonempty(rec->source_field)) {
        return SS_SDK_ERR_VALIDATION;
    }

    if (rec->data_kind != SS_SDK_DATA_OBSERVATION && rec->data_kind != SS_SDK_DATA_FORECAST) {
        return SS_SDK_ERR_VALIDATION;
    }

    if (rec->data_kind == SS_SDK_DATA_FORECAST) {
        if (!ss_sdk_is_nonempty(rec->model_id)) {
            return SS_SDK_ERR_VALIDATION;
        }
        if (rec->model_run_utc <= 0 || rec->issued_at_utc <= 0) {
            return SS_SDK_ERR_VALIDATION;
        }
    }

    if (rec->value_type == SS_SDK_VALUE_STR && rec->value.str == NULL) {
        return SS_SDK_ERR_VALIDATION;
    }

    meta = ss_metric_meta_get(rec->metric);
    if (meta == NULL) {
        return SS_SDK_ERR_VALIDATION;
    }
    /* Enforce canonical type contract from SDK metric catalog. */
    if (rec->value_type != meta->value_type) {
        return SS_SDK_ERR_VALIDATION;
    }

    return SS_SDK_OK;
}

ss_sdk_status ss_sdk_db_write_record(const ss_sdk_record *record)
{
    ss_sdk_status rc = ss_sdk_validate_record(record);
    if (rc != SS_SDK_OK) {
        return rc;
    }
    return ss_sdk_internal_db_write_record(record);
}

ss_sdk_status ss_sdk_db_get_last_weeks(int weeks, ss_sdk_record **out_records, size_t *out_count)
{
    if (out_records == NULL || out_count == NULL || weeks < 0) {
        return SS_SDK_ERR_INVALID_ARG;
    }
    return ss_sdk_internal_db_get_last_weeks(weeks, out_records, out_count);
}

void ss_sdk_db_free_records(ss_sdk_record *records)
{
    ss_sdk_internal_db_free_records(records);
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
    ss_sdk_internal_log_shutdown();
}
