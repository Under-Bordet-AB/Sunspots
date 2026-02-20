#ifndef SS_SDK_H
#define SS_SDK_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "sdk/ss_canonical.h"
#include "sdk/ss_sdk_types.h"

/**
 * @brief SDK status/error codes returned by public API calls.
 */
typedef enum {
    SS_SDK_OK = 0,         /**< Call succeeded. */
    SS_SDK_ERR_PARTIAL_DATA, /**< Data completeness signal: data may be usable, but request completeness was not met. */
    SS_SDK_ERR_INVALID_ARG,  /**< Invalid function arguments. */
    SS_SDK_ERR_VALIDATION,   /**< Input failed SDK validation rules. */
    SS_SDK_ERR_NOT_IMPLEMENTED, /**< API/feature not implemented. */
    SS_SDK_ERR_INTERNAL      /**< Internal/config/I/O failure. */
} ss_sdk_status;

/**
 * @brief Generic typed value container used by canonical records.
 */
typedef union {
    int64_t i64;
    double f64;
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

// Canonical record persisted by SDK DB backend.
typedef struct ss_sdk_record {
    ss_metric_id metric; // Canonical metric identifier from SDK catalog.
    ss_sdk_value_type value_type; // Value type expected by metric metadata.
    ss_sdk_value value; // Actual value payload.
    int64_t ts_start_utc; // 15-minute slot start timestamp (UTC epoch seconds).
    int64_t ts_end_utc; // 15-minute slot end timestamp (UTC epoch seconds).
    ss_sdk_data_kind data_kind; // Observation/forecast marker.
} ss_sdk_record;

typedef uint8_t ss_sdk_sample_flags;

enum {
    SS_SDK_SAMPLE_OBSERVED = 1U << 0,
    SS_SDK_SAMPLE_FORECAST = 1U << 1,
    SS_SDK_SAMPLE_INTERPOLATED = 1U << 2
};

typedef struct {
    int64_t ts_utc;
    ss_metric_id canonical;
    ss_sdk_value_type value_type;
    ss_sdk_value value;
    ss_sdk_sample_flags flags;
} ss_sdk_sample;

typedef struct {
    ss_sdk_sample *samples;
    size_t count;
} ss_sdk_samples_out;

// Optional structured logging context for advanced log calls.
typedef struct ss_sdk_log_fields {
    const char *module; // Logical module name (example: "fetch.openmeteo").
    const char *source_api; // Optional source API identifier.
    ss_metric_id metric; // Optional metric associated with log event.
    int64_t ts_utc; // Optional event timestamp (UTC epoch seconds).
} ss_sdk_log_fields;

/**
 * @brief Validate and persist one canonical record.
 *
 * Behavior:
 * - `record->ts_start_utc` is normalized down to nearest 15-minute UTC slot.
 * - `ts_end_utc` is normalized to `ts_start_utc + 900` before persistence.
 *
 * @param record Input canonical record.
 * @return SDK status code.
 */
ss_sdk_status ss_sdk_db_write_record(const ss_sdk_record *record);

/**
 * @brief Factory: build a canonical record for an f64 metric.
 *
 * Behavior:
 * - Validates metric exists and expects f64 values.
 * - Validates data_kind.
 * - Normalizes `ts_utc` down to nearest 15-minute UTC slot.
 * - Sets `ts_end_utc = ts_start_utc + 900`.
 */
ss_sdk_status ss_sdk_record_make_f64(
    ss_sdk_record *out,
    ss_metric_id metric,
    double value,
    int64_t ts_utc,
    ss_sdk_data_kind data_kind);

/**
 * @brief Factory: build a canonical record for an i64 metric.
 */
ss_sdk_status ss_sdk_record_make_i64(
    ss_sdk_record *out,
    ss_metric_id metric,
    int64_t value,
    int64_t ts_utc,
    ss_sdk_data_kind data_kind);

/**
 * @brief Factory: build a canonical record for a bool metric.
 */
ss_sdk_status ss_sdk_record_make_bool(
    ss_sdk_record *out,
    ss_metric_id metric,
    bool value,
    int64_t ts_utc,
    ss_sdk_data_kind data_kind);

/**
 * @brief Read one canonical across a 15-minute slot window or forward horizon.
 *
 * Behavior:
 * - If `from_utc == 0`, the query starts at the current 15-minute UTC slot.
 * - If `from_utc > 0`, it is aligned down to the nearest 15-minute UTC slot.
 * - If `quarters_to_fetch > 0`, fetch exactly that many 15-minute slots.
 * - If `quarters_to_fetch == 0`, fetch from start slot through the latest
 *   available stored slot for that canonical metric (forward horizon).
 *
 * @param from_utc Input start timestamp (UTC epoch seconds), or `0` for "now slot".
 * @param quarters_to_fetch Number of 15-minute slots to fetch, or `0` for forward horizon.
 * @param canonical Canonical metric identifier.
 * @param out SDK-allocated output sample array.
 * @return SDK status code.
 */
ss_sdk_status ss_sdk_db_get_canonical(
    int64_t from_utc,
    uint16_t quarters_to_fetch,
    ss_metric_id canonical,
    ss_sdk_samples_out *out);

/**
 * @brief Release sample array returned by `ss_sdk_db_get_canonical`.
 *
 * @param out Output struct returned by SDK read API.
 */
void ss_sdk_db_free_samples(ss_sdk_samples_out *out);





/**
 * @brief Log helper macro (debug level) with source location.
 * @return `ss_sdk_status`
 */
#ifndef SS_SDK_LOG_FILE_TOKEN
#if defined(__FILE_NAME__)
#define SS_SDK_LOG_FILE_TOKEN __FILE_NAME__
#else
#define SS_SDK_LOG_FILE_TOKEN __FILE__
#endif
#endif

#define SS_LOG_DEBUG(event, message) ss_sdk_log_write_auto(SS_SDK_LOG_DEBUG, (event), (message), SS_SDK_LOG_FILE_TOKEN, __LINE__, __func__)
/**
 * @brief Log helper macro (info level) with source location.
 * @return `ss_sdk_status`
 */
#define SS_LOG_INFO(event, message)  ss_sdk_log_write_auto(SS_SDK_LOG_INFO,  (event), (message), SS_SDK_LOG_FILE_TOKEN, __LINE__, __func__)
/**
 * @brief Log helper macro (warning level) with source location.
 * @return `ss_sdk_status`
 */
#define SS_LOG_WARN(event, message)  ss_sdk_log_write_auto(SS_SDK_LOG_WARN,  (event), (message), SS_SDK_LOG_FILE_TOKEN, __LINE__, __func__)
/**
 * @brief Log helper macro (error level) with source location.
 * @return `ss_sdk_status`
 */
#define SS_LOG_ERROR(event, message) ss_sdk_log_write_auto(SS_SDK_LOG_ERROR, (event), (message), SS_SDK_LOG_FILE_TOKEN, __LINE__, __func__)

/**
 * @brief Write one log event with auto-captured source location.
 *
 * @param level Log severity.
 * @param event Optional stable event key (`NULL` or empty allowed).
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
    const char *func);

/**
 * @brief Write one structured log event with optional extra fields.
 *
 * Use this variant when you want extra typed context beyond event/message.
 *
 * @param level Log severity.
 * @param event Optional stable event key (`NULL` or empty allowed).
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
    const char *func);

/**
 * @brief Optional SDK shutdown hook.
 */
void ss_sdk_shutdown(void);

#ifdef SS_SDK_ENABLE_TEST_HOOKS
/**
 * @brief Test hook: override SDK wall-clock source.
 *
 * @param enabled Non-zero to use `now_value`, 0 to use system `time(NULL)`.
 * @param now_value Forced wall-clock value used when enabled.
 */
void ss_sdk_test_set_now_override(int enabled, int64_t now_value);
#endif

#endif
