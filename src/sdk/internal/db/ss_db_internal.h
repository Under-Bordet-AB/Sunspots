#ifndef SS_DB_INTERNAL_H
#define SS_DB_INTERNAL_H

#include "sdk/ss_sdk.h"

/**
 * @brief Internal DB write entrypoint used by SDK facade.
 *
 * @param record Canonical input record.
 * @return SDK status code.
 */
ss_sdk_status ss_sdk_internal_db_write_record(const ss_sdk_record *record);

/**
 * @brief Check whether a canonical record identity is already persisted.
 *
 * Identity follows the SDK dedupe contract (metric/timestamp/kind/model/source).
 *
 * @param record Canonical input record.
 * @param out_exists True if an equivalent identity already exists.
 * @return SDK status code.
 */
ss_sdk_status ss_sdk_internal_db_record_exists(const ss_sdk_record *record, bool *out_exists);

/**
 * @brief Internal DB read entrypoint used by SDK facade.
 *
 * @param weeks Weeks window, caller already validated for non-negative input.
 * @param out_records SDK-allocated output records.
 * @param out_count Number of returned records.
 * @return SDK status code.
 */
ss_sdk_status ss_sdk_internal_db_get_last_weeks(int weeks, ss_sdk_record **out_records, size_t *out_count);

/**
 * @brief Release records returned by internal read function.
 *
 * @param records Pointer returned by internal DB read API.
 */
void ss_sdk_internal_db_free_records(ss_sdk_record *records);

#endif
