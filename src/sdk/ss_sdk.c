#include "sdk/ss_sdk.h"

#include <math.h>
#include <string.h>

#include "sdk/internal/db/ss_db_internal.h"
#include "sdk/internal/log/ss_log_internal.h"

typedef struct ss_sdk_session_state {
    bool active;
    ss_sdk_session_config cfg;
} ss_sdk_session_state;

static ss_sdk_session_state g_ss_sdk_session = {0};

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

static ss_sdk_status ss_sdk_validate_session_config(const ss_sdk_session_config *cfg)
{
    if (cfg == NULL) {
        return SS_SDK_ERR_INVALID_ARG;
    }
    if (!ss_sdk_is_nonempty(cfg->source_api)) {
        return SS_SDK_ERR_INVALID_ARG;
    }
    if (!ss_sdk_is_nonempty(cfg->source_tz)) {
        return SS_SDK_ERR_INVALID_ARG;
    }
    if (cfg->default_data_kind != SS_SDK_DATA_OBSERVATION && cfg->default_data_kind != SS_SDK_DATA_FORECAST) {
        return SS_SDK_ERR_INVALID_ARG;
    }
    if (cfg->default_data_kind == SS_SDK_DATA_FORECAST) {
        if (!ss_sdk_is_nonempty(cfg->default_model_id)) {
            return SS_SDK_ERR_INVALID_ARG;
        }
        if (cfg->default_model_run_utc <= 0 || cfg->default_issued_at_utc <= 0) {
            return SS_SDK_ERR_INVALID_ARG;
        }
    }
    return SS_SDK_OK;
}

static ss_sdk_status ss_sdk_apply_f64_transform(double *value, ss_sdk_value_transform xform)
{
    if (value == NULL) {
        return SS_SDK_ERR_INVALID_ARG;
    }
    switch (xform) {
        case SS_SDK_XFORM_NONE:
            return SS_SDK_OK;
        case SS_SDK_XFORM_F_TO_C:
            *value = (*value - 32.0) * (5.0 / 9.0);
            return SS_SDK_OK;
        case SS_SDK_XFORM_KPH_TO_MS:
            *value = *value / 3.6;
            return SS_SDK_OK;
        case SS_SDK_XFORM_MPH_TO_MS:
            *value = *value * 0.44704;
            return SS_SDK_OK;
        default:
            return SS_SDK_ERR_INVALID_ARG;
    }
}

static ss_sdk_status ss_sdk_make_point_record(
    ss_sdk_record *rec,
    ss_metric_id metric,
    ss_sdk_value_type value_type,
    ss_sdk_value value,
    int64_t ts_utc,
    const char *source_field)
{
    const ss_sdk_session_config *cfg = &g_ss_sdk_session.cfg;

    if (rec == NULL || !g_ss_sdk_session.active) {
        return SS_SDK_ERR_INVALID_ARG;
    }
    if (!ss_sdk_is_nonempty(source_field)) {
        return SS_SDK_ERR_INVALID_ARG;
    }
    if (ts_utc <= 0) {
        return SS_SDK_ERR_INVALID_ARG;
    }

    memset(rec, 0, sizeof(*rec));
    rec->metric = metric;
    rec->value_type = value_type;
    rec->value = value;
    rec->ts_start_utc = ts_utc;
    rec->ts_end_utc = ts_utc;
    rec->data_kind = cfg->default_data_kind;
    rec->source_api = cfg->source_api;
    rec->source_field = source_field;
    rec->source_tz = cfg->source_tz;
    rec->model_id = cfg->default_model_id;
    rec->model_run_utc = cfg->default_model_run_utc;
    rec->issued_at_utc = cfg->default_issued_at_utc;
    return SS_SDK_OK;
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

ss_sdk_status ss_sdk_db_record_exists(const ss_sdk_record *record, bool *out_exists)
{
    ss_sdk_status rc;

    if (out_exists == NULL) {
        return SS_SDK_ERR_INVALID_ARG;
    }
    *out_exists = false;

    rc = ss_sdk_validate_record(record);
    if (rc != SS_SDK_OK) {
        return rc;
    }
    return ss_sdk_internal_db_record_exists(record, out_exists);
}

ss_sdk_status ss_sdk_session_begin(const ss_sdk_session_config *cfg)
{
    ss_sdk_status rc = ss_sdk_validate_session_config(cfg);
    if (rc != SS_SDK_OK) {
        return rc;
    }
    g_ss_sdk_session.cfg = *cfg;
    g_ss_sdk_session.active = true;
    return SS_SDK_OK;
}

void ss_sdk_session_end(void)
{
    memset(&g_ss_sdk_session, 0, sizeof(g_ss_sdk_session));
}

bool ss_sdk_session_is_active(void)
{
    return g_ss_sdk_session.active;
}

ss_sdk_status ss_sdk_db_write_f64(
    ss_metric_id metric,
    double value,
    int64_t ts_utc,
    const char *source_field,
    ss_sdk_value_transform xform)
{
    ss_sdk_record rec;
    ss_sdk_value v;
    ss_sdk_status rc;

    if (!isfinite(value)) {
        return SS_SDK_ERR_INVALID_ARG;
    }

    rc = ss_sdk_apply_f64_transform(&value, xform);
    if (rc != SS_SDK_OK) {
        return rc;
    }

    v.f64 = value;
    rc = ss_sdk_make_point_record(&rec, metric, SS_SDK_VALUE_F64, v, ts_utc, source_field);
    if (rc != SS_SDK_OK) {
        return rc;
    }
    return ss_sdk_db_write_record(&rec);
}

ss_sdk_status ss_sdk_db_write_i64(
    ss_metric_id metric,
    int64_t value,
    int64_t ts_utc,
    const char *source_field)
{
    ss_sdk_record rec;
    ss_sdk_value v;
    ss_sdk_status rc;

    v.i64 = value;
    rc = ss_sdk_make_point_record(&rec, metric, SS_SDK_VALUE_I64, v, ts_utc, source_field);
    if (rc != SS_SDK_OK) {
        return rc;
    }
    return ss_sdk_db_write_record(&rec);
}

ss_sdk_status ss_sdk_db_write_bool(
    ss_metric_id metric,
    bool value,
    int64_t ts_utc,
    const char *source_field)
{
    ss_sdk_record rec;
    ss_sdk_value v;
    ss_sdk_status rc;

    v.boolean = value;
    rc = ss_sdk_make_point_record(&rec, metric, SS_SDK_VALUE_BOOL, v, ts_utc, source_field);
    if (rc != SS_SDK_OK) {
        return rc;
    }
    return ss_sdk_db_write_record(&rec);
}

ss_sdk_status ss_sdk_db_write_str(
    ss_metric_id metric,
    const char *value,
    int64_t ts_utc,
    const char *source_field)
{
    ss_sdk_record rec;
    ss_sdk_value v;
    ss_sdk_status rc;

    if (value == NULL) {
        return SS_SDK_ERR_INVALID_ARG;
    }
    v.str = value;
    rc = ss_sdk_make_point_record(&rec, metric, SS_SDK_VALUE_STR, v, ts_utc, source_field);
    if (rc != SS_SDK_OK) {
        return rc;
    }
    return ss_sdk_db_write_record(&rec);
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
    ss_sdk_session_end();
}
