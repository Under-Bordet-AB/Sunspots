#ifndef SS_DB_INTERNAL_SHARED_H
#define SS_DB_INTERNAL_SHARED_H

#include "sdk/ss_sdk.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sqlite3.h>

enum {
    SS_SLOT_SECONDS = 900,
    SS_DB_INTERP_PADDING_SLOTS = 24,
    SS_DB_INTERP_MAX_LENGTH_SECONDS = 6 * 3600
};

typedef enum {
    SS_INTERP_POLICY_INVALID = -1,
    SS_INTERP_POLICY_NONE = 0,
    SS_INTERP_POLICY_LINEAR,
    SS_INTERP_POLICY_STEP
} ss_interp_policy;

typedef struct {
    int64_t ts_utc;
    ss_sdk_data_kind data_kind;
    ss_sdk_value_type value_type;
    ss_sdk_value value;
} ss_raw_row;

typedef enum {
    SS_DB_SQL_SELECT_ROWS_WINDOW = 0,
    SS_DB_SQL_INSERT_RECORD,
    SS_DB_SQL_SELECT_MAX_TS_FROM_START,
    SS_DB_SQL_COUNT
} ss_db_sql_id;

const char *ss_db_sql_text(ss_db_sql_id sql_id);

bool ss_value_type_is_supported(ss_sdk_value_type value_type);
ss_interp_policy ss_interpolation_policy(ss_metric_id canonical);
bool ss_all_metrics_have_interpolation_policy(void);
int64_t ss_now_slot_utc(void);
bool ss_raw_rows_append(ss_raw_row **rows, size_t *count, size_t *cap, const ss_raw_row *row);
bool ss_find_exact_row(
    const ss_raw_row *rows,
    size_t count,
    int64_t ts_utc,
    ss_sdk_data_kind data_kind,
    ss_raw_row *out);
bool ss_try_interpolate(
    ss_metric_id canonical,
    ss_sdk_value_type value_type,
    const ss_raw_row *rows,
    size_t count,
    int64_t ts_utc,
    bool allow_observation,
    bool allow_forecast,
    bool *out_interpolation_length_exceeded,
    ss_sdk_value *out_value);
ss_sdk_status ss_load_rows_for_window(
    sqlite3 *db,
    ss_metric_id canonical,
    ss_sdk_value_type expected_type,
    int64_t start_utc,
    int64_t end_utc,
    ss_raw_row **out_rows,
    size_t *out_count);

#ifdef SS_SDK_ENABLE_TEST_HOOKS
typedef struct {
    int fail_strdup;
    int fail_mkdir;
    int fail_sqlite_open;
    int fail_sqlite_exec;
    int fail_sqlite_prepare;
    int fail_sqlite_step;
    int fail_sqlite_close;
    int fail_realloc;
    int fail_calloc;
    int force_interp_incomplete;
    int force_now_negative;
    int force_linear_zero_span;
    int force_policy_unknown;
} ss_db_test_hooks;

extern ss_db_test_hooks g_db_test_hooks;
int ss_test_consume(int *slot);
#endif

#endif
