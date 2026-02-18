#include "sdk/internal/db/ss_db_internal.h"
#include "sdk/internal/db/ss_db_internal_shared.h"

#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <sqlite3.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

static const char *const SS_SDK_DB_DEFAULT_PATH = "db/ss_sdk.db";

// Lookup table for all SQL queries
static const char *const g_ss_db_sql[SS_DB_SQL_COUNT] = {
    [SS_DB_SQL_SELECT_ROWS_WINDOW] =
        "SELECT ts_start_utc, data_kind, value_type, value_i64, value_f64, value_bool "
        "FROM records "
        "WHERE canonical = ?1 AND ts_start_utc >= ?2 AND ts_start_utc < ?3 "
        "ORDER BY ts_start_utc ASC, data_kind ASC",
    [SS_DB_SQL_INSERT_RECORD] =
        "INSERT INTO records("
        "canonical, value_type, value_i64, value_f64, value_bool, ts_start_utc, ts_end_utc, data_kind"
        ") VALUES(?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8) "
        "ON CONFLICT(canonical, data_kind, ts_start_utc) DO NOTHING",
    [SS_DB_SQL_SELECT_MAX_TS_FROM_START] =
        "SELECT MAX(ts_start_utc) "
        "FROM records "
        "WHERE canonical = ?1 AND ts_start_utc >= ?2"
};

// returns SQL string form lookup table
const char *ss_db_sql_text(ss_db_sql_id sql_id)
{
    if ((int)sql_id < 0 || sql_id >= SS_DB_SQL_COUNT) {
        return NULL;
    }
    return g_ss_db_sql[sql_id];
}

// DB singelton locked with mutex to prevent submodules with multi-threaded access to corrupt open, R/W or close states.
// (SQLite manages the actual db file but the mutex protects modules from using the "door" into the db incorrectly)
static pthread_mutex_t g_db_mu = PTHREAD_MUTEX_INITIALIZER;
static sqlite3 *g_db = NULL;
static char *g_db_open_path = NULL;

#ifdef SS_SDK_ENABLE_TEST_HOOKS
ss_db_test_hooks g_db_test_hooks;

int ss_test_consume(int *slot)
{
    if (slot == NULL || *slot <= 0) {
        return 0;
    }
    *slot -= 1;
    return 1;
}

void ss_sdk_internal_db_test_reset_hooks(void)
{
    memset(&g_db_test_hooks, 0, sizeof(g_db_test_hooks));
}

void ss_sdk_internal_db_test_set_hook(ss_sdk_db_test_hook hook, int count)
{
    int safe_count = (count < 0) ? 0 : count;
    switch (hook) {
        case SS_SDK_DB_HOOK_FAIL_STRDUP:
            g_db_test_hooks.fail_strdup = safe_count;
            break;
        case SS_SDK_DB_HOOK_FAIL_MKDIR:
            g_db_test_hooks.fail_mkdir = safe_count;
            break;
        case SS_SDK_DB_HOOK_FAIL_SQLITE_OPEN:
            g_db_test_hooks.fail_sqlite_open = safe_count;
            break;
        case SS_SDK_DB_HOOK_FAIL_SQLITE_EXEC:
            g_db_test_hooks.fail_sqlite_exec = safe_count;
            break;
        case SS_SDK_DB_HOOK_FAIL_SQLITE_PREPARE:
            g_db_test_hooks.fail_sqlite_prepare = safe_count;
            break;
        case SS_SDK_DB_HOOK_FAIL_SQLITE_STEP:
            g_db_test_hooks.fail_sqlite_step = safe_count;
            break;
        case SS_SDK_DB_HOOK_FAIL_SQLITE_CLOSE:
            g_db_test_hooks.fail_sqlite_close = safe_count;
            break;
        case SS_SDK_DB_HOOK_FAIL_REALLOC:
            g_db_test_hooks.fail_realloc = safe_count;
            break;
        case SS_SDK_DB_HOOK_FAIL_CALLOC:
            g_db_test_hooks.fail_calloc = safe_count;
            break;
        case SS_SDK_DB_HOOK_FORCE_INTERPOLATION_MAP_INCOMPLETE:
            g_db_test_hooks.force_interp_incomplete = safe_count;
            break;
        case SS_SDK_DB_HOOK_FORCE_NOW_NEGATIVE:
            g_db_test_hooks.force_now_negative = safe_count;
            break;
        case SS_SDK_DB_HOOK_FORCE_LINEAR_ZERO_SPAN:
            g_db_test_hooks.force_linear_zero_span = safe_count;
            break;
        case SS_SDK_DB_HOOK_FORCE_POLICY_UNKNOWN:
            g_db_test_hooks.force_policy_unknown = safe_count;
            break;
        default:
            break;
    }
}
#endif

static char *ss_strdup_local(const char *s)
{
    if (s == NULL) {
        return NULL;
    }
// must be below nullcheck so hook does not trigger on invalid input
#ifdef SS_SDK_ENABLE_TEST_HOOKS
    if (ss_test_consume(&g_db_test_hooks.fail_strdup)) {
        errno = ENOMEM;
        return NULL;
    }
#endif

    size_t n;
    char *p;
    n = strlen(s) + 1U;
    p = (char *)malloc(n);

    if (p == NULL) {
        return NULL;
    }

    memcpy(p, s, n);
    return p;
}

static const char *ss_db_path(void)
{
    // if we have env variable set, read from that instead
    const char *override = getenv("SS_SDK_DB_PATH");
    if (override != NULL && override[0] != '\0') {
        return override;
    }

    return SS_SDK_DB_DEFAULT_PATH;
}

// Create missing parent folders. Walks path backwards and creates any missing folders in path.
static int ss_ensure_parent_dirs(const char *path)
{
    char *path_copy;
    char *slash_cursor;

    if (path == NULL || path[0] == '\0') {
        errno = EINVAL;
        return -1;
    }

    path_copy = ss_strdup_local(path);
    if (path_copy == NULL) {
        return -1;
    }

    for (slash_cursor = path_copy + 1; *slash_cursor != '\0'; ++slash_cursor) {
        if (*slash_cursor != '/') {
            continue;
        }

        *slash_cursor = '\0';
#ifdef SS_SDK_ENABLE_TEST_HOOKS
        if (ss_test_consume(&g_db_test_hooks.fail_mkdir)) {
            errno = EACCES;
            free(path_copy);
            return -1;
        }
#endif
        if (path_copy[0] != '\0' && mkdir(path_copy, 0775) != 0 && errno != EEXIST) {
            free(path_copy);
            return -1;
        }
        *slash_cursor = '/';
    }

    free(path_copy);
    return 0;
}

// Thin wrapper around sqlite3_exec to keep DB error mapping and SQLite error
// message cleanup in one place; test hooks can force a deterministic failure.
static ss_sdk_status ss_exec(sqlite3 *db, const char *sql)
{
    int sqlite_result;
    char *sqlite_error_message = NULL;

#ifdef SS_SDK_ENABLE_TEST_HOOKS
    if (g_db_test_hooks.fail_sqlite_exec > 0) {
        g_db_test_hooks.fail_sqlite_exec -= 1;
        if (g_db_test_hooks.fail_sqlite_exec == 0) {
            return SS_SDK_ERR_INTERNAL;
        }
    }
#endif

    sqlite_result = sqlite3_exec(db, sql, NULL, NULL, &sqlite_error_message);
    if (sqlite_result != SQLITE_OK) {
        sqlite3_free(sqlite_error_message);
        return SS_SDK_ERR_INTERNAL;
    }

    return SS_SDK_OK;
}

// "pragmas" in SQL are settings
static ss_sdk_status ss_db_apply_pragmas(sqlite3 *db)
{
    ss_sdk_status status;

    status = ss_exec(db, "PRAGMA journal_mode=WAL;");
    if (status != SS_SDK_OK) {
        return status;
    }

    status = ss_exec(db, "PRAGMA synchronous=NORMAL;");
    if (status != SS_SDK_OK) {
        return status;
    }

    status = ss_exec(db, "PRAGMA busy_timeout=5000;");
    if (status != SS_SDK_OK) {
        return status;
    }

    status = ss_exec(db, "PRAGMA wal_autocheckpoint=1000;");
    if (status != SS_SDK_OK) {
        return status;
    }

    return SS_SDK_OK;
}

static ss_sdk_status ss_db_create_canonical_data_schema(sqlite3 *db)
{
    static const char *k_canonical_data_schema_sql =
        "CREATE TABLE IF NOT EXISTS records ("
        "canonical INTEGER NOT NULL,"
        "value_type INTEGER NOT NULL,"
        "value_i64 INTEGER,"
        "value_f64 REAL,"
        "value_bool INTEGER,"
        "ts_start_utc INTEGER NOT NULL,"
        "ts_end_utc INTEGER NOT NULL,"
        "data_kind INTEGER NOT NULL,"
        "CHECK(canonical >= 0),"
        "CHECK(value_type IN (0,1,2)),"
        "CHECK(data_kind IN (0,1)),"
        "CHECK(ts_start_utc >= 0),"
        "CHECK(ts_start_utc % 900 = 0),"
        "CHECK(ts_end_utc = ts_start_utc + 900),"
        "CHECK((value_type = 0 AND value_i64 IS NOT NULL AND typeof(value_i64) = 'integer' AND value_f64 IS NULL AND value_bool IS NULL)"
        "   OR (value_type = 1 AND value_f64 IS NOT NULL AND (typeof(value_f64) = 'real' OR typeof(value_f64) = 'integer') AND value_i64 IS NULL AND value_bool IS NULL)"
        "   OR (value_type = 2 AND value_bool IN (0,1) AND typeof(value_bool) = 'integer' AND value_i64 IS NULL AND value_f64 IS NULL)),"
        "UNIQUE(canonical, data_kind, ts_start_utc)"
        ") STRICT;"
        "CREATE INDEX IF NOT EXISTS idx_records_canonical_data_kind_ts "
        "ON records(canonical, data_kind, ts_start_utc);"
        "CREATE INDEX IF NOT EXISTS idx_records_canonical_ts "
        "ON records(canonical, ts_start_utc);";

    return ss_exec(db, k_canonical_data_schema_sql);
}

static void ss_db_close_locked(void)
{
    if (g_db != NULL) {
        int sqlite_result;

        (void)sqlite3_wal_checkpoint_v2(g_db, NULL, SQLITE_CHECKPOINT_PASSIVE, NULL, NULL);
        sqlite_result = sqlite3_close(g_db);
#ifdef SS_SDK_ENABLE_TEST_HOOKS
        if (ss_test_consume(&g_db_test_hooks.fail_sqlite_close)) {
            sqlite_result = SQLITE_BUSY;
        }
#endif
        if (sqlite_result != SQLITE_OK) {
            (void)sqlite3_close_v2(g_db);
        }
        g_db = NULL;
    }

    free(g_db_open_path);
    g_db_open_path = NULL;
}

static ss_sdk_status ss_db_open_locked(void)
{
    const char *path = ss_db_path();
    sqlite3 *opened_db = NULL;
    ss_sdk_status status;
    int sqlite_result;

    bool db_is_open = (g_db != NULL);
    bool paths_match = db_is_open &&
                g_db_open_path != NULL &&
                strcmp(g_db_open_path, path) == 0;

    // 1) Already open on requested path: reuse existing handle.
    if (paths_match) {
        return SS_SDK_OK;
    }

    // 2) Open on a different path: close old handle before switching.
    if (db_is_open) {
        ss_db_close_locked();
    }

    // 3) First open or switched path: ensure parent folders exist.
    if (ss_ensure_parent_dirs(path) != 0) {
        return SS_SDK_ERR_INTERNAL;
    }

    sqlite_result = sqlite3_open_v2(path, &opened_db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX, NULL);
#ifdef SS_SDK_ENABLE_TEST_HOOKS
    if (g_db_test_hooks.fail_sqlite_open > 0) {
        g_db_test_hooks.fail_sqlite_open -= 1;
        if (g_db_test_hooks.fail_sqlite_open == 0) {
            sqlite_result = SQLITE_ERROR;
        }
    }
#endif
    if (sqlite_result != SQLITE_OK || opened_db == NULL) {
        if (opened_db != NULL) {
            (void)sqlite3_close(opened_db);
        }
        return SS_SDK_ERR_INTERNAL;
    }

    // apply DB settings
    status = ss_db_apply_pragmas(opened_db);
    if (status != SS_SDK_OK) {
        (void)sqlite3_close(opened_db);
        return status;
    }
    
    status = ss_db_create_canonical_data_schema(opened_db);
    if (status != SS_SDK_OK) {
        (void)sqlite3_close(opened_db);
        return status;
    }

    g_db_open_path = ss_strdup_local(path);
    if (g_db_open_path == NULL) {
        (void)sqlite3_close(opened_db);
        return SS_SDK_ERR_INTERNAL;
    }

    g_db = opened_db;
    return SS_SDK_OK;
}

ss_sdk_status ss_sdk_internal_db_write_record(const ss_sdk_record *record)
{ 
    if (record == NULL) {
        return SS_SDK_ERR_INVALID_ARG;
    }

    const char *sql_text = ss_db_sql_text(SS_DB_SQL_INSERT_RECORD);

    sqlite3_stmt *insert_statment = NULL;
    ss_sdk_status status = SS_SDK_ERR_INTERNAL;
    int sqlite_result = SQLITE_OK;
    int bind_error_count = 0;
    int lock_held = 0;
    const char *log_event = NULL;
    const char *log_message = NULL;

    pthread_mutex_lock(&g_db_mu);
    lock_held = 1;

    status = ss_db_open_locked();
    if (status != SS_SDK_OK) {
        log_event = "sdk.db.open_failed";
        log_message = "failed to open sqlite connection for write";
        goto cleanup;
    }

    // Compile SQL text into prepared statement (parse + validate SQL).
    sqlite_result = sqlite3_prepare_v2(g_db, sql_text, -1, &insert_statment, NULL);
#ifdef SS_SDK_ENABLE_TEST_HOOKS
    if (ss_test_consume(&g_db_test_hooks.fail_sqlite_prepare)) {
        sqlite_result = SQLITE_ERROR;
    }
#endif
    if (sqlite_result != SQLITE_OK) {
        status = SS_SDK_ERR_INTERNAL;
        log_event = "sdk.db.prepare_failed";
        log_message = "sqlite prepare failed for insert statement";
        goto cleanup;
    }

    // Bind calls just fill the VALUES slots in insert_statment; no DB write happens until sqlite3_step.
    bind_error_count += (sqlite3_bind_int(insert_statment, 1, (int)record->metric) != SQLITE_OK);              // ?1 canonical metric id
    bind_error_count += (sqlite3_bind_int(insert_statment, 2, (int)record->value_type) != SQLITE_OK);          // ?2 value type discriminator

    // Schema CHECK rules enforce union-like storage: value_type selects exactly one of
    // value_i64/value_f64/value_bool to be non-NULL, and the other two must be SQL NULL.
    if (record->value_type == SS_SDK_VALUE_I64) {
        bind_error_count += (sqlite3_bind_int64(insert_statment, 3, (sqlite3_int64)record->value.i64) != SQLITE_OK); // ?3 integer value payload
        bind_error_count += (sqlite3_bind_null(insert_statment, 4) != SQLITE_OK);                                        // Typed storage rule: only one value column is set; others are bound as SQL NULL.
        bind_error_count += (sqlite3_bind_null(insert_statment, 5) != SQLITE_OK);
    } else if (record->value_type == SS_SDK_VALUE_F64) {
        bind_error_count += (sqlite3_bind_null(insert_statment, 3) != SQLITE_OK);
        bind_error_count += (sqlite3_bind_double(insert_statment, 4, record->value.f64) != SQLITE_OK);                // ?4 real value payload
        bind_error_count += (sqlite3_bind_null(insert_statment, 5) != SQLITE_OK);
    } else if (record->value_type == SS_SDK_VALUE_BOOL) {
        bind_error_count += (sqlite3_bind_null(insert_statment, 3) != SQLITE_OK);
        bind_error_count += (sqlite3_bind_null(insert_statment, 4) != SQLITE_OK);
        bind_error_count += (sqlite3_bind_int(insert_statment, 5, record->value.boolean ? 1 : 0) != SQLITE_OK);       // ?5 bool value payload
    } else {
        status = SS_SDK_ERR_VALIDATION;
        log_event = "sdk.db.bind_invalid_type";
        log_message = "record value_type was invalid during bind";
        goto cleanup;
    }

    bind_error_count += (sqlite3_bind_int64(insert_statment, 6, (sqlite3_int64)record->ts_start_utc) != SQLITE_OK);  // ?6 slot start (UTC)
    bind_error_count += (sqlite3_bind_int64(insert_statment, 7, (sqlite3_int64)record->ts_end_utc) != SQLITE_OK);    // ?7 slot end (UTC)
    bind_error_count += (sqlite3_bind_int(insert_statment, 8, (int)record->data_kind) != SQLITE_OK);                 // ?8 observation/forecast kind

    if (bind_error_count != 0) {
        status = SS_SDK_ERR_INTERNAL;
        log_event = "sdk.db.bind_failed";
        log_message = "sqlite bind failed for insert statement";
        goto cleanup;
    }

    // Execute the prepared SQL against SQLite now (sends statment into SQLite, blocking but should be fast).
    sqlite_result = sqlite3_step(insert_statment);
#ifdef SS_SDK_ENABLE_TEST_HOOKS
    if (ss_test_consume(&g_db_test_hooks.fail_sqlite_step)) {
        sqlite_result = SQLITE_ERROR;
    }
#endif
    if (sqlite_result == SQLITE_DONE) {
        if (sqlite3_changes(g_db) == 0) {
            log_event = "sdk.db.dedupe_drop";
            log_message = "duplicate canonical slot ignored";
        }
        status = SS_SDK_OK;
    } else if (sqlite_result == SQLITE_CONSTRAINT) {
        status = SS_SDK_ERR_VALIDATION;
        log_event = "sdk.db.step_constraint";
        log_message = "sqlite constraint rejected insert";
    } else {
        status = SS_SDK_ERR_INTERNAL;
        log_event = "sdk.db.step_internal_error";
        log_message = "sqlite step failed during insert";
    }

cleanup:
    if (insert_statment != NULL) {
        sqlite3_finalize(insert_statment);
    }
    if (lock_held) {
        pthread_mutex_unlock(&g_db_mu);
    }
    if (log_event != NULL && log_message != NULL) {
        if (strcmp(log_event, "sdk.db.dedupe_drop") == 0) {
            SS_LOG_DEBUG(log_event, log_message);
        } else {
            SS_LOG_ERROR(log_event, log_message);
        }
    }
    return status;
}

ss_sdk_status ss_sdk_internal_db_get_canonical(
    ss_metric_id canonical,
    int64_t start_utc,
    int64_t end_utc,
    ss_sdk_samples_out *out)
{
    const ss_metric_meta *metric_metadata;
    ss_raw_row *window_rows = NULL;
    size_t window_row_count = 0;
    ss_sdk_sample *samples = NULL;
    size_t sample_count = 0;
    size_t expected_slots;
    int64_t now_slot;
    int64_t ts_utc;
    ss_interp_policy policy;
    ss_sdk_status status;

    if (out == NULL) {
        return SS_SDK_ERR_INVALID_ARG;
    }

    out->samples = NULL;
    out->count = 0;

    if (start_utc < 0 || end_utc <= start_utc || ((end_utc - start_utc) % SS_SLOT_SECONDS) != 0) {
        return SS_SDK_ERR_INVALID_ARG;
    }

    metric_metadata = ss_metric_meta_get(canonical);
    if (metric_metadata == NULL || !ss_value_type_is_supported(metric_metadata->value_type)) {
        return SS_SDK_ERR_INVALID_ARG;
    }

    if (!ss_all_metrics_have_interpolation_policy()) {
        return SS_SDK_ERR_INTERNAL;
    }
    policy = ss_interpolation_policy(canonical);
    if (policy == SS_INTERP_POLICY_INVALID) {
        return SS_SDK_ERR_INTERNAL;
    }

    expected_slots = (size_t)((end_utc - start_utc) / SS_SLOT_SECONDS);
#ifdef SS_SDK_ENABLE_TEST_HOOKS
    if (ss_test_consume(&g_db_test_hooks.fail_calloc)) {
        samples = NULL;
    } else {
        samples = (ss_sdk_sample *)calloc(expected_slots, sizeof(ss_sdk_sample));
    }
#else
    samples = (ss_sdk_sample *)calloc(expected_slots, sizeof(ss_sdk_sample));
#endif
    if (samples == NULL) {
        return SS_SDK_ERR_INTERNAL;
    }

    pthread_mutex_lock(&g_db_mu);

    status = ss_db_open_locked();
    if (status != SS_SDK_OK) {
        pthread_mutex_unlock(&g_db_mu);
        free(samples);
        return status;
    }

    status = ss_load_rows_for_window(g_db, canonical, metric_metadata->value_type, start_utc, end_utc, &window_rows, &window_row_count);
    pthread_mutex_unlock(&g_db_mu);
    if (status != SS_SDK_OK) {
        free(samples);
        return status;
    }

    now_slot = ss_now_slot_utc();

    for (ts_utc = start_utc; ts_utc < end_utc; ts_utc += SS_SLOT_SECONDS) {
        ss_raw_row selected_row;
        bool have_value = false;
        bool is_future = (ts_utc > now_slot);
        bool allow_obs_interp;
        bool allow_fc_interp;
        ss_sdk_sample_flags sample_flags = 0;

        if (!is_future) {
            if (ss_find_exact_row(window_rows, window_row_count, ts_utc, SS_SDK_DATA_OBSERVATION, &selected_row)) {
                have_value = true;
                sample_flags = SS_SDK_SAMPLE_OBSERVED;
            } else if (ss_find_exact_row(window_rows, window_row_count, ts_utc, SS_SDK_DATA_FORECAST, &selected_row)) {
                have_value = true;
                sample_flags = SS_SDK_SAMPLE_FORECAST;
            }
        } else {
            if (ss_find_exact_row(window_rows, window_row_count, ts_utc, SS_SDK_DATA_FORECAST, &selected_row)) {
                have_value = true;
                sample_flags = SS_SDK_SAMPLE_FORECAST;
            } else if (policy == SS_INTERP_POLICY_STEP &&
                       ss_find_exact_row(window_rows, window_row_count, ts_utc, SS_SDK_DATA_OBSERVATION, &selected_row)) {
                have_value = true;
                sample_flags = SS_SDK_SAMPLE_OBSERVED;
            }
        }

        if (!have_value) {
            ss_sdk_value interpolated;
            bool interpolation_length_exceeded = false;

            allow_obs_interp = (!is_future) || (policy == SS_INTERP_POLICY_STEP);
            allow_fc_interp = true;
            if (ss_try_interpolate(
                    canonical,
                    metric_metadata->value_type,
                    window_rows,
                    window_row_count,
                    ts_utc,
                    allow_obs_interp,
                    allow_fc_interp,
                    &interpolation_length_exceeded,
                    &interpolated)) {
                selected_row.ts_utc = ts_utc;
                selected_row.data_kind = SS_SDK_DATA_FORECAST;
                selected_row.value_type = metric_metadata->value_type;
                selected_row.value = interpolated;
                have_value = true;
                sample_flags = SS_SDK_SAMPLE_INTERPOLATED;
            } else if (interpolation_length_exceeded) {
                free(window_rows);
                free(samples);
                out->samples = NULL;
                out->count = 0;
                return SS_SDK_ERR_PARTIAL_DATA;
            }
        }

        if (!have_value) {
            continue;
        }

        samples[sample_count].ts_utc = ts_utc;
        samples[sample_count].canonical = canonical;
        samples[sample_count].value_type = metric_metadata->value_type;
        samples[sample_count].value = selected_row.value;
        samples[sample_count].flags = sample_flags;
        sample_count += 1U;
    }

    free(window_rows);

    if (sample_count == 0U) {
        free(samples);
        out->samples = NULL;
        out->count = 0;
        return SS_SDK_ERR_PARTIAL_DATA;
    }

    if (sample_count < expected_slots) {
        ss_sdk_sample *shrunk = (ss_sdk_sample *)realloc(samples, sample_count * sizeof(ss_sdk_sample));
        if (shrunk != NULL) {
            samples = shrunk;
        }
        out->samples = samples;
        out->count = sample_count;
        return SS_SDK_ERR_PARTIAL_DATA;
    }

    out->samples = samples;
    out->count = sample_count;
    return SS_SDK_OK;
}

ss_sdk_status ss_sdk_internal_db_get_canonical_forward(
    ss_metric_id canonical,
    int64_t start_utc,
    ss_sdk_samples_out *out)
{
    const char *sql_text = ss_db_sql_text(SS_DB_SQL_SELECT_MAX_TS_FROM_START);

    sqlite3_stmt *max_statement = NULL;
    int sqlite_result;
    int has_value = 0;
    int64_t max_ts_start = 0;
    int64_t end_utc;
    ss_sdk_status status;

    if (out == NULL || start_utc < 0) {
        return SS_SDK_ERR_INVALID_ARG;
    }
    out->samples = NULL;
    out->count = 0;

    pthread_mutex_lock(&g_db_mu);
    status = ss_db_open_locked();
    if (status != SS_SDK_OK) {
        pthread_mutex_unlock(&g_db_mu);
        return status;
    }

    sqlite_result = sqlite3_prepare_v2(g_db, sql_text, -1, &max_statement, NULL);
    if (sqlite_result != SQLITE_OK) {
        if (max_statement != NULL) {
            sqlite3_finalize(max_statement);
        }
        pthread_mutex_unlock(&g_db_mu);
        return SS_SDK_ERR_INTERNAL;
    }

    sqlite3_bind_int(max_statement, 1, (int)canonical);
    sqlite3_bind_int64(max_statement, 2, (sqlite3_int64)start_utc);

    sqlite_result = sqlite3_step(max_statement);
    if (sqlite_result == SQLITE_ROW) {
        if (sqlite3_column_type(max_statement, 0) != SQLITE_NULL) {
            max_ts_start = (int64_t)sqlite3_column_int64(max_statement, 0);
            has_value = 1;
        }
    } else if (sqlite_result != SQLITE_DONE) {
        sqlite3_finalize(max_statement);
        pthread_mutex_unlock(&g_db_mu);
        return SS_SDK_ERR_INTERNAL;
    }

    sqlite3_finalize(max_statement);
    pthread_mutex_unlock(&g_db_mu);

    if (!has_value) {
        return SS_SDK_ERR_PARTIAL_DATA;
    }

    if (max_ts_start > INT64_MAX - SS_SLOT_SECONDS) {
        return SS_SDK_ERR_INVALID_ARG;
    }
    end_utc = max_ts_start + SS_SLOT_SECONDS;

    return ss_sdk_internal_db_get_canonical(canonical, start_utc, end_utc, out);
}

void ss_sdk_internal_db_free_samples(ss_sdk_samples_out *out)
{
    if (out == NULL) {
        return;
    }

    free(out->samples);
    out->samples = NULL;
    out->count = 0;
}

void ss_sdk_internal_db_shutdown(void)
{
    pthread_mutex_lock(&g_db_mu);
    ss_db_close_locked();
    pthread_mutex_unlock(&g_db_mu);
}

#ifdef SS_SDK_ENABLE_TEST_HOOKS
int ss_sdk_internal_db_test_ensure_parent_dirs(const char *path)
{
    return ss_ensure_parent_dirs(path);
}

const char *ss_sdk_internal_db_test_db_path(void)
{
    return ss_db_path();
}

bool ss_sdk_internal_db_test_interpolation_map_complete(void)
{
    return ss_all_metrics_have_interpolation_policy();
}

int64_t ss_sdk_internal_db_test_now_slot_utc(void)
{
    return ss_now_slot_utc();
}

char *ss_sdk_internal_db_test_strdup_local(const char *s)
{
    return ss_strdup_local(s);
}

int ss_sdk_internal_db_test_consume_null_slot(void)
{
    return ss_test_consume(NULL);
}

int ss_sdk_internal_db_test_interpolation_policy(ss_metric_id canonical)
{
    return (int)ss_interpolation_policy(canonical);
}

bool ss_sdk_internal_db_test_raw_rows_append_overflow(void)
{
    ss_raw_row row;
    ss_raw_row *rows = NULL;
    size_t cap = (SIZE_MAX / sizeof(ss_raw_row)) + 1U;
    size_t count = cap;

    memset(&row, 0, sizeof(row));
    return ss_raw_rows_append(&rows, &count, &cap, &row);
}

ss_sdk_status ss_sdk_internal_db_test_exec_sql(const char *sql)
{
    ss_sdk_status status;

    if (sql == NULL) {
        return SS_SDK_ERR_INVALID_ARG;
    }

    pthread_mutex_lock(&g_db_mu);
    status = ss_db_open_locked();
    if (status == SS_SDK_OK) {
        status = ss_exec(g_db, sql);
    }
    pthread_mutex_unlock(&g_db_mu);

    return status;
}

ss_sdk_status ss_sdk_internal_db_test_load_rows_for_window(
    ss_metric_id canonical,
    ss_sdk_value_type expected_type,
    int64_t start_utc,
    int64_t end_utc,
    size_t *out_count)
{
    ss_sdk_status status;
    ss_raw_row *rows = NULL;
    size_t count = 0;

    if (out_count == NULL) {
        return SS_SDK_ERR_INVALID_ARG;
    }
    *out_count = 0;

    pthread_mutex_lock(&g_db_mu);
    status = ss_db_open_locked();
    if (status == SS_SDK_OK) {
        status = ss_load_rows_for_window(g_db, canonical, expected_type, start_utc, end_utc, &rows, &count);
    }
    pthread_mutex_unlock(&g_db_mu);

    if (status == SS_SDK_OK) {
        *out_count = count;
    }
    free(rows);
    return status;
}

bool ss_sdk_internal_db_test_try_interpolate_linear_nonf64(void)
{
    ss_raw_row rows[2];
    ss_sdk_value out_value;

    memset(rows, 0, sizeof(rows));
    rows[0].ts_utc = 0;
    rows[0].value_type = SS_SDK_VALUE_F64;
    rows[0].value.f64 = 1.0;
    rows[1].ts_utc = 1800;
    rows[1].value_type = SS_SDK_VALUE_F64;
    rows[1].value.f64 = 3.0;

    return ss_try_interpolate(
        SS_METRIC_WEATHER_TEMPERATURE_AIR_2M_C,
        SS_SDK_VALUE_I64,
        rows,
        2U,
        900,
        true,
        true,
        NULL,
        &out_value);
}

bool ss_sdk_internal_db_test_try_interpolate_zero_span(void)
{
    ss_raw_row rows[2];
    ss_sdk_value out_value;
    bool interpolation_ok;

    memset(rows, 0, sizeof(rows));
    rows[0].ts_utc = 0;
    rows[0].value_type = SS_SDK_VALUE_F64;
    rows[0].value.f64 = 1.0;
    rows[1].ts_utc = 1800;
    rows[1].value_type = SS_SDK_VALUE_F64;
    rows[1].value.f64 = 3.0;

    ss_sdk_internal_db_test_set_hook(SS_SDK_DB_HOOK_FORCE_LINEAR_ZERO_SPAN, 1);
    interpolation_ok = ss_try_interpolate(
        SS_METRIC_WEATHER_TEMPERATURE_AIR_2M_C,
        SS_SDK_VALUE_F64,
        rows,
        2U,
        900,
        true,
        true,
        NULL,
        &out_value);
    ss_sdk_internal_db_test_set_hook(SS_SDK_DB_HOOK_FORCE_LINEAR_ZERO_SPAN, 0);
    return interpolation_ok;
}

bool ss_sdk_internal_db_test_try_interpolate_unknown_policy(void)
{
    ss_raw_row rows[2];
    ss_sdk_value out_value;
    bool interpolation_ok;

    memset(rows, 0, sizeof(rows));
    rows[0].ts_utc = 0;
    rows[0].value_type = SS_SDK_VALUE_F64;
    rows[0].value.f64 = 1.0;
    rows[1].ts_utc = 1800;
    rows[1].value_type = SS_SDK_VALUE_F64;
    rows[1].value.f64 = 3.0;

    ss_sdk_internal_db_test_set_hook(SS_SDK_DB_HOOK_FORCE_POLICY_UNKNOWN, 1);
    interpolation_ok = ss_try_interpolate(
        SS_METRIC_WEATHER_TEMPERATURE_AIR_2M_C,
        SS_SDK_VALUE_F64,
        rows,
        2U,
        900,
        true,
        true,
        NULL,
        &out_value);
    ss_sdk_internal_db_test_set_hook(SS_SDK_DB_HOOK_FORCE_POLICY_UNKNOWN, 0);
    return interpolation_ok;
}
#endif
