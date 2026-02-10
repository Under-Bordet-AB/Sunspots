#ifndef SS_SDK_H
#define SS_SDK_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "sdk/ss_sdk_types.h"
#include "sdk/ss_canonical.h"

/** Maximum accepted weeks for `ss_sdk_db_get_last_weeks`. */
#define SS_SDK_DB_MAX_WEEKS 8

/**
 * @brief SDK status/error codes returned by public API calls.
 */
typedef enum {
    SS_SDK_OK = 0,
    SS_SDK_ERR_INVALID_ARG,
    SS_SDK_ERR_VALIDATION,
    SS_SDK_ERR_NOT_IMPLEMENTED,
    SS_SDK_ERR_INTERNAL
} ss_sdk_status;

/**
 * @brief Generic typed value container used by canonical records.
 */
typedef union {
    int64_t i64;
    double f64;
    const char *str;
    bool boolean;
} ss_sdk_value;

/**
 * @brief Distinguishes observed values from forecast values.
 */
typedef enum {
    SS_SDK_DATA_OBSERVATION = 0,
    SS_SDK_DATA_FORECAST
} ss_sdk_data_kind;

/**
 * @brief SDK log severity levels.
 */
typedef enum {
    SS_SDK_LOG_DEBUG = 0,
    SS_SDK_LOG_INFO,
    SS_SDK_LOG_WARN,
    SS_SDK_LOG_ERROR
} ss_sdk_log_level;

/**
 * @brief Single conversion applied by SDK typed write helpers.
 */
typedef enum {
    SS_SDK_XFORM_NONE = 0,
    SS_SDK_XFORM_F_TO_C,
    SS_SDK_XFORM_KPH_TO_MS,
    SS_SDK_XFORM_MPH_TO_MS
} ss_sdk_value_transform;

/**
 * @brief Canonical record persisted by SDK DB backend.
 */
typedef struct ss_sdk_record {
    /** Canonical metric identifier from SDK catalog. */
    ss_metric_id metric;
    /** Value type expected by metric metadata. */
    ss_sdk_value_type value_type;
    /** Actual value payload. */
    ss_sdk_value value;
    /** Interval start timestamp (UTC epoch seconds). */
    int64_t ts_start_utc;
    /** Interval end timestamp (UTC epoch seconds). */
    int64_t ts_end_utc;
    /** Observation/forecast marker. */
    ss_sdk_data_kind data_kind;
    /** Provider identifier (example: "openmeteo.forecast"). */
    const char *source_api;
    /** Original provider field/key name. */
    const char *source_field;
    /** Provider timezone label. */
    const char *source_tz;
    /** Forecast model identifier (required for forecasts). */
    const char *model_id;
    /** Forecast model run timestamp (UTC epoch seconds). */
    int64_t model_run_utc;
    /** Forecast issuance timestamp (UTC epoch seconds). */
    int64_t issued_at_utc;
} ss_sdk_record;

/**
 * @brief Process-local defaults used by typed SDK write helpers.
 */
typedef struct ss_sdk_session_config {
    /** Stable provider identifier for this process/session. */
    const char *source_api;
    /** Default timezone for this provider/session. */
    const char *source_tz;
    /** Default data kind for typed write helpers. */
    ss_sdk_data_kind default_data_kind;
    /** Optional default forecast model ID. */
    const char *default_model_id;
    /** Optional default forecast model run UTC epoch. */
    int64_t default_model_run_utc;
    /** Optional default forecast issued UTC epoch. */
    int64_t default_issued_at_utc;
} ss_sdk_session_config;

/**
 * @brief Optional structured logging context for advanced log calls.
 */
typedef struct ss_sdk_log_fields {
    /** Logical module name (example: "fetch.openmeteo"). */
    const char *module;
    /** Optional source API identifier. */
    const char *source_api;
    /** Optional metric associated with log event. */
    ss_metric_id metric;
    /** Optional event timestamp (UTC epoch seconds). */
    int64_t ts_utc;
} ss_sdk_log_fields;

/**
 * @brief Initialize process-local SDK session defaults for typed write helpers.
 *
 * @param cfg Session configuration.
 * @return SDK status code.
 */
ss_sdk_status ss_sdk_session_begin(const ss_sdk_session_config *cfg);

/**
 * @brief Clear process-local SDK session defaults.
 */
void ss_sdk_session_end(void);

/**
 * @brief Check whether process-local SDK session defaults are active.
 *
 * @return true when typed writes can use session defaults.
 */
bool ss_sdk_session_is_active(void);

/**
 * @brief Write one f64 metric using active session defaults.
 *
 * @param metric Canonical metric identifier.
 * @param value Value to write.
 * @param ts_utc UTC epoch seconds, used as point timestamp.
 * @param source_field Provider field/key name.
 * @param xform Optional single conversion before persistence.
 * @return SDK status code.
 */
ss_sdk_status ss_sdk_db_write_f64(
    ss_metric_id metric,
    double value,
    int64_t ts_utc,
    const char *source_field,
    ss_sdk_value_transform xform
);

/**
 * @brief Write one i64 metric using active session defaults.
 *
 * @param metric Canonical metric identifier.
 * @param value Value to write.
 * @param ts_utc UTC epoch seconds, used as point timestamp.
 * @param source_field Provider field/key name.
 * @return SDK status code.
 */
ss_sdk_status ss_sdk_db_write_i64(
    ss_metric_id metric,
    int64_t value,
    int64_t ts_utc,
    const char *source_field
);

/**
 * @brief Write one bool metric using active session defaults.
 *
 * @param metric Canonical metric identifier.
 * @param value Value to write.
 * @param ts_utc UTC epoch seconds, used as point timestamp.
 * @param source_field Provider field/key name.
 * @return SDK status code.
 */
ss_sdk_status ss_sdk_db_write_bool(
    ss_metric_id metric,
    bool value,
    int64_t ts_utc,
    const char *source_field
);

/**
 * @brief Write one string metric using active session defaults.
 *
 * @param metric Canonical metric identifier.
 * @param value Value to write.
 * @param ts_utc UTC epoch seconds, used as point timestamp.
 * @param source_field Provider field/key name.
 * @return SDK status code.
 */
ss_sdk_status ss_sdk_db_write_str(
    ss_metric_id metric,
    const char *value,
    int64_t ts_utc,
    const char *source_field
);

/**
 * @brief Validate and persist one canonical record.
 *
 * @param record Input canonical record.
 * @return SDK status code.
 */
ss_sdk_status ss_sdk_db_write_record(const ss_sdk_record *record);

/**
 * @brief Check whether a record identity is already present in storage.
 *
 * Uses the same identity fields as SDK dedupe on write.
 *
 * @param record Canonical input record.
 * @param out_exists True if equivalent record identity exists.
 * @return SDK status code.
 */
ss_sdk_status ss_sdk_db_record_exists(const ss_sdk_record *record, bool *out_exists);

/**
 * @brief Read canonical records from recent history.
 *
 * Query returns all metrics from `now - weeks` forward (including prognosis).
 * `weeks == 0` returns now-forward records only.
 *
 * @param weeks Window size in weeks, silently capped by `SS_SDK_DB_MAX_WEEKS`.
 * @param out_records SDK-allocated array output.
 * @param out_count Number of elements returned in `out_records`.
 * @return SDK status code.
 */
ss_sdk_status ss_sdk_db_get_last_weeks(int weeks, ss_sdk_record **out_records, size_t *out_count);

/**
 * @brief Release record array returned by `ss_sdk_db_get_last_weeks`.
 *
 * @param records Array pointer returned by SDK read API.
 */
void ss_sdk_db_free_records(ss_sdk_record *records);

/**
 * @brief Log helper macro (debug level) with source location.
 */
#define SS_LOG_DEBUG(event, message) ss_sdk_log_write_auto(SS_SDK_LOG_DEBUG, (event), (message), __FILE__, __LINE__, __func__)
/**
 * @brief Log helper macro (info level) with source location.
 */
#define SS_LOG_INFO(event, message)  ss_sdk_log_write_auto(SS_SDK_LOG_INFO,  (event), (message), __FILE__, __LINE__, __func__)
/**
 * @brief Log helper macro (warning level) with source location.
 */
#define SS_LOG_WARN(event, message)  ss_sdk_log_write_auto(SS_SDK_LOG_WARN,  (event), (message), __FILE__, __LINE__, __func__)
/**
 * @brief Log helper macro (error level) with source location.
 */
#define SS_LOG_ERROR(event, message) ss_sdk_log_write_auto(SS_SDK_LOG_ERROR, (event), (message), __FILE__, __LINE__, __func__)

/**
 * @brief Write one log event with auto-captured source location.
 *
 * @param level Log severity.
 * @param event Stable event key.
 * @param message Human-readable message.
 * @param file Source file path.
 * @param line Source line number.
 * @param func Source function name.
 * @return SDK status code.
 */
ss_sdk_status ss_sdk_log_write_auto(
    ss_sdk_log_level level,
    const char *event,
    const char *message,
    const char *file,
    int line,
    const char *func
);

/**
 * @brief Write one structured log event with optional extra fields.
 *
 * @param level Log severity.
 * @param event Stable event key.
 * @param message Human-readable message.
 * @param fields Optional structured context.
 * @param file Source file path.
 * @param line Source line number.
 * @param func Source function name.
 * @return SDK status code.
 */
ss_sdk_status ss_sdk_log_write_fields(
    ss_sdk_log_level level,
    const char *event,
    const char *message,
    const ss_sdk_log_fields *fields,
    const char *file,
    int line,
    const char *func
);

/**
 * @brief Optional SDK shutdown hook.
 */
void ss_sdk_shutdown(void);

#endif
