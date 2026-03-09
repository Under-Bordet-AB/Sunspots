#ifndef SS_DB_INTERNAL_H
#define SS_DB_INTERNAL_H

#include "sdk/ss_sdk.h"

ss_sdk_status ss_sdk_internal_db_write_record(const ss_sdk_record *record);
ss_sdk_status ss_sdk_internal_db_write_records(
    const ss_sdk_record *records,
    size_t count,
    size_t *out_written);

ss_sdk_status ss_sdk_internal_db_get_canonical(
    ss_metric_id canonical,
    int64_t start_utc,
    int64_t end_utc,
    ss_sdk_samples_out *out);

ss_sdk_status ss_sdk_internal_db_get_canonical_forward(
    ss_metric_id canonical,
    int64_t start_utc,
    ss_sdk_samples_out *out);

ss_sdk_status ss_sdk_internal_db_clamp_end_utc(
    ss_metric_id canonical,
    int64_t start_utc,
    int64_t requested_end_utc,
    int64_t *out_end_utc,
    int *out_was_clamped);

void ss_sdk_internal_db_free_samples(ss_sdk_samples_out *out);

void ss_sdk_internal_db_shutdown(void);

#ifdef SS_SDK_ENABLE_TEST_HOOKS
typedef enum {
    SS_SDK_DB_HOOK_FAIL_STRDUP = 0,
    SS_SDK_DB_HOOK_FAIL_MKDIR,
    SS_SDK_DB_HOOK_FAIL_SQLITE_OPEN,
    SS_SDK_DB_HOOK_FAIL_SQLITE_EXEC,
    SS_SDK_DB_HOOK_FAIL_SQLITE_PREPARE,
    SS_SDK_DB_HOOK_FAIL_SQLITE_STEP,
    SS_SDK_DB_HOOK_FAIL_SQLITE_CLOSE,
    SS_SDK_DB_HOOK_FAIL_REALLOC,
    SS_SDK_DB_HOOK_FAIL_CALLOC,
    SS_SDK_DB_HOOK_FORCE_INTERPOLATION_MAP_INCOMPLETE,
    SS_SDK_DB_HOOK_FORCE_NOW_NEGATIVE,
    SS_SDK_DB_HOOK_FORCE_LINEAR_ZERO_SPAN,
    SS_SDK_DB_HOOK_FORCE_POLICY_UNKNOWN
} ss_sdk_db_test_hook;

void ss_sdk_internal_db_test_reset_hooks(void);
void ss_sdk_internal_db_test_set_hook(ss_sdk_db_test_hook hook, int count);

int ss_sdk_internal_db_test_ensure_parent_dirs(const char *path);
const char *ss_sdk_internal_db_test_db_path(void);
bool ss_sdk_internal_db_test_interpolation_map_complete(void);
int64_t ss_sdk_internal_db_test_now_slot_utc(void);
char *ss_sdk_internal_db_test_strdup_local(const char *s);
int ss_sdk_internal_db_test_consume_null_slot(void);
int ss_sdk_internal_db_test_interpolation_policy(ss_metric_id canonical);
bool ss_sdk_internal_db_test_raw_rows_append_overflow(void);
ss_sdk_status ss_sdk_internal_db_test_exec_sql(const char *sql);
ss_sdk_status ss_sdk_internal_db_test_load_rows_for_window(
    ss_metric_id canonical,
    ss_sdk_value_type expected_type,
    int64_t start_utc,
    int64_t end_utc,
    size_t *out_count);
bool ss_sdk_internal_db_test_try_interpolate_linear_nonf64(void);
bool ss_sdk_internal_db_test_try_interpolate_zero_span(void);
bool ss_sdk_internal_db_test_try_interpolate_unknown_policy(void);
#endif

#endif
