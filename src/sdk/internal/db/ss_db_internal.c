#include "sdk/internal/db/ss_db_internal.h"
#include "sdk/internal/db/ss_db_internal_shared.h"
#include "sdk/internal/ss_sdk_config.h"
#include "sdk/internal/ss_sdk_config_util.h"

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
#include <time.h>
#include <unistd.h>

// Lookup table for all SQL queries
static const char *const g_ss_db_sql[SS_DB_SQL_COUNT] = {
    [SS_DB_SQL_SELECT_ROWS_WINDOW] =
        "SELECT ts_start_utc, data_kind, value_type, value_i64, value_f64, value_bool, ingested_utc "
        "FROM records "
        "WHERE canonical = ?1 AND ts_start_utc >= ?2 AND ts_start_utc < ?3 "
        "ORDER BY ts_start_utc ASC, data_kind ASC, ingested_utc DESC, rowid DESC",
    [SS_DB_SQL_INSERT_RECORD] =
        "INSERT INTO records("
        "canonical, value_type, value_i64, value_f64, value_bool, ts_start_utc, ts_end_utc, data_kind, ingested_utc"
        ") VALUES(?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9) "
        "ON CONFLICT(canonical, data_kind, ts_start_utc, ingested_utc) DO NOTHING",
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

typedef struct {
    double latitude;
    double longitude;
    long long lat_micro;
    long long lon_micro;
    char location_id[96];
    char nickname[64];
    char elprisomrade[16];
} ss_location_ctx;

typedef struct {
    int has_identity;
    char location_id[96];
    long long lat_micro;
    long long lon_micro;
} ss_location_identity_row;

typedef struct {
    int has_meta;
    char nickname[64];
    char elprisomrade[16];
} ss_location_meta_row;

typedef struct {
    ss_sdk_status status;
    const char *log_event;
    const char *log_message;
} ss_db_write_result;

static ss_sdk_status ss_exec(sqlite3 *db, const char *sql);

static long long ss_coord_to_micro(double v)
{
    double scaled = v * 1000000.0;
    if (scaled >= 0.0) {
        return (long long)(scaled + 0.5);
    }
    return (long long)(scaled - 0.5);
}

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

static void ss_db_debug_log(const char *event, const char *message)
{
    (void)SS_LOG_DEBUG(event, message);
}

static void ss_db_debug_log_path(const char *event, const char *path)
{
    char msg[SS_SDK_PATH_BUFFER_SIZE];
    if (path == NULL) {
        path = "";
    }
    if (snprintf(msg, sizeof(msg), "db_path=%s", path) < 0) {
        return;
    }
    (void)SS_LOG_DEBUG(event, msg);
}

static int ss_location_ctx_from_system_env(ss_location_ctx *out)
{
    ss_sdk_cfg_location location;
    ss_sdk_cfg_status status;

    if (out == NULL) {
        return -1;
    }
    memset(out, 0, sizeof(*out));

    status = ss_sdk_cfg_get_location_from_system_env(&location);
    if (status != SS_SDK_CFG_OK) {
        return -1;
    }

    out->latitude = location.latitude;
    out->longitude = location.longitude;

    out->lat_micro = ss_coord_to_micro(out->latitude);
    out->lon_micro = ss_coord_to_micro(out->longitude);
    if (snprintf(out->location_id, sizeof(out->location_id), "loc_%lld_%lld", out->lat_micro, out->lon_micro) <= 0) {
        return -1;
    }
    (void)snprintf(out->nickname, sizeof(out->nickname), "%s", location.name);
    (void)snprintf(out->elprisomrade, sizeof(out->elprisomrade), "%s", location.elprisomrade);
    return 0;
}

static const char *ss_db_path(void)
{
    static int sdk_env_bootstrapped = 0;
    static char dynamic_path[SS_SDK_PATH_BUFFER_SIZE];
    ss_location_ctx loc;
    const char *dir_override;
    int n = 0;

    if (!sdk_env_bootstrapped) {
        ss_sdk_config_bootstrap_env_from_blob();
        sdk_env_bootstrapped = 1;
    }

    dir_override = getenv(SS_SDK_ENV_DB_DIR);
    if (dir_override == NULL || dir_override[0] == '\0') {
        dir_override = SS_SDK_DB_DEFAULT_DIR;
    }

    if (ss_location_ctx_from_system_env(&loc) != 0) {
        return NULL;
    }
    n = snprintf(dynamic_path, sizeof(dynamic_path), "%s/%lld_%lld.db", dir_override, loc.lat_micro, loc.lon_micro);
    if (n <= 0 || (size_t)n >= sizeof(dynamic_path)) {
        return NULL;
    }
    return dynamic_path;
}

static ss_sdk_status ss_db_ensure_location_meta_schema(sqlite3 *db)
{
    static const char *k_meta_schema_sql =
        "CREATE TABLE IF NOT EXISTS location_identity ("
        "location_id TEXT PRIMARY KEY,"
        "latitude REAL NOT NULL,"
        "longitude REAL NOT NULL,"
        "created_utc INTEGER NOT NULL"
        ") STRICT;"
        "CREATE TABLE IF NOT EXISTS location_meta_history ("
        "effective_from_utc INTEGER NOT NULL,"
        "nickname TEXT NOT NULL,"
        "elprisomrade TEXT NOT NULL"
        ") STRICT;"
        "CREATE INDEX IF NOT EXISTS idx_location_meta_history_effective "
        "ON location_meta_history(effective_from_utc);";
    return ss_exec(db, k_meta_schema_sql);
}

static ss_sdk_status ss_db_read_identity_row(sqlite3 *db, ss_location_identity_row *out_row)
{
    static const char *k_select_identity_sql =
        "SELECT location_id, latitude, longitude FROM location_identity LIMIT 1;";
    sqlite3_stmt *stmt = NULL;
    int sqlite_result;

    if (db == NULL || out_row == NULL) {
        return SS_SDK_ERR_INVALID_ARG;
    }
    memset(out_row, 0, sizeof(*out_row));

    sqlite_result = sqlite3_prepare_v2(db, k_select_identity_sql, -1, &stmt, NULL);
    if (sqlite_result != SQLITE_OK) {
        return SS_SDK_ERR_INTERNAL;
    }

    sqlite_result = sqlite3_step(stmt);
    if (sqlite_result == SQLITE_ROW) {
        const unsigned char *id_text = sqlite3_column_text(stmt, 0);
        double lat = sqlite3_column_double(stmt, 1);
        double lon = sqlite3_column_double(stmt, 2);
        if (id_text == NULL) {
            sqlite3_finalize(stmt);
            return SS_SDK_ERR_INTERNAL;
        }
        (void)snprintf(out_row->location_id, sizeof(out_row->location_id), "%s", (const char *)id_text);
        out_row->lat_micro = ss_coord_to_micro(lat);
        out_row->lon_micro = ss_coord_to_micro(lon);
        out_row->has_identity = 1;
    } else if (sqlite_result != SQLITE_DONE) {
        sqlite3_finalize(stmt);
        return SS_SDK_ERR_INTERNAL;
    }

    sqlite3_finalize(stmt);
    return SS_SDK_OK;
}

static ss_sdk_status ss_db_insert_identity_row(sqlite3 *db, const ss_location_ctx *loc)
{
    static const char *k_insert_identity_sql =
        "INSERT INTO location_identity(location_id, latitude, longitude, created_utc) VALUES(?1, ?2, ?3, ?4);";
    sqlite3_stmt *stmt = NULL;
    int sqlite_result;

    if (db == NULL || loc == NULL) {
        return SS_SDK_ERR_INVALID_ARG;
    }

    sqlite_result = sqlite3_prepare_v2(db, k_insert_identity_sql, -1, &stmt, NULL);
    if (sqlite_result != SQLITE_OK) {
        return SS_SDK_ERR_INTERNAL;
    }
    sqlite3_bind_text(stmt, 1, loc->location_id, -1, SQLITE_TRANSIENT);
    sqlite3_bind_double(stmt, 2, loc->latitude);
    sqlite3_bind_double(stmt, 3, loc->longitude);
    sqlite3_bind_int64(stmt, 4, (sqlite3_int64)time(NULL));
    sqlite_result = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return (sqlite_result == SQLITE_DONE) ? SS_SDK_OK : SS_SDK_ERR_INTERNAL;
}

static ss_sdk_status ss_db_validate_or_insert_identity(
    sqlite3 *db,
    const ss_location_ctx *loc,
    const ss_location_identity_row *identity_row)
{
    if (db == NULL || loc == NULL || identity_row == NULL) {
        return SS_SDK_ERR_INVALID_ARG;
    }

    if (!identity_row->has_identity) {
        return ss_db_insert_identity_row(db, loc);
    }

    if (strcmp(identity_row->location_id, loc->location_id) != 0 || identity_row->lat_micro != loc->lat_micro ||
        identity_row->lon_micro != loc->lon_micro) {
        return SS_SDK_ERR_INTERNAL;
    }
    return SS_SDK_OK;
}

static ss_sdk_status ss_db_read_latest_meta_row(sqlite3 *db, ss_location_meta_row *out_row)
{
    static const char *k_select_latest_meta_sql =
        "SELECT nickname, elprisomrade FROM location_meta_history ORDER BY effective_from_utc DESC LIMIT 1;";
    sqlite3_stmt *stmt = NULL;
    int sqlite_result;

    if (db == NULL || out_row == NULL) {
        return SS_SDK_ERR_INVALID_ARG;
    }
    memset(out_row, 0, sizeof(*out_row));

    sqlite_result = sqlite3_prepare_v2(db, k_select_latest_meta_sql, -1, &stmt, NULL);
    if (sqlite_result != SQLITE_OK) {
        return SS_SDK_ERR_INTERNAL;
    }

    sqlite_result = sqlite3_step(stmt);
    if (sqlite_result == SQLITE_ROW) {
        const unsigned char *nick = sqlite3_column_text(stmt, 0);
        const unsigned char *area = sqlite3_column_text(stmt, 1);
        if (nick != NULL) {
            (void)snprintf(out_row->nickname, sizeof(out_row->nickname), "%s", (const char *)nick);
        }
        if (area != NULL) {
            (void)snprintf(out_row->elprisomrade, sizeof(out_row->elprisomrade), "%s", (const char *)area);
        }
        out_row->has_meta = 1;
    } else if (sqlite_result != SQLITE_DONE) {
        sqlite3_finalize(stmt);
        return SS_SDK_ERR_INTERNAL;
    }

    sqlite3_finalize(stmt);
    return SS_SDK_OK;
}

static ss_sdk_status ss_db_insert_meta_row(sqlite3 *db, const ss_location_ctx *loc)
{
    static const char *k_insert_meta_sql =
        "INSERT INTO location_meta_history(effective_from_utc, nickname, elprisomrade) VALUES(?1, ?2, ?3);";
    sqlite3_stmt *stmt = NULL;
    int sqlite_result;

    if (db == NULL || loc == NULL) {
        return SS_SDK_ERR_INVALID_ARG;
    }

    sqlite_result = sqlite3_prepare_v2(db, k_insert_meta_sql, -1, &stmt, NULL);
    if (sqlite_result != SQLITE_OK) {
        return SS_SDK_ERR_INTERNAL;
    }
    sqlite3_bind_int64(stmt, 1, (sqlite3_int64)time(NULL));
    sqlite3_bind_text(stmt, 2, loc->nickname, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, loc->elprisomrade, -1, SQLITE_TRANSIENT);
    sqlite_result = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return (sqlite_result == SQLITE_DONE) ? SS_SDK_OK : SS_SDK_ERR_INTERNAL;
}

static ss_sdk_status ss_db_append_meta_if_changed(sqlite3 *db, const ss_location_ctx *loc, const ss_location_meta_row *meta_row)
{
    if (db == NULL || loc == NULL || meta_row == NULL) {
        return SS_SDK_ERR_INVALID_ARG;
    }
    if (!meta_row->has_meta || strcmp(meta_row->nickname, loc->nickname) != 0 ||
        strcmp(meta_row->elprisomrade, loc->elprisomrade) != 0) {
        return ss_db_insert_meta_row(db, loc);
    }
    return SS_SDK_OK;
}

static ss_sdk_status ss_db_sync_location_metadata(sqlite3 *db)
{
    ss_location_ctx loc;
    ss_location_identity_row identity_row;
    ss_location_meta_row meta_row;
    ss_sdk_status status;

    if (ss_location_ctx_from_system_env(&loc) != 0) {
        return SS_SDK_ERR_INTERNAL;
    }

    status = ss_db_ensure_location_meta_schema(db);
    if (status != SS_SDK_OK) {
        return status;
    }

    status = ss_db_read_identity_row(db, &identity_row);
    if (status != SS_SDK_OK) {
        return status;
    }
    status = ss_db_validate_or_insert_identity(db, &loc, &identity_row);
    if (status != SS_SDK_OK) {
        return status;
    }

    status = ss_db_read_latest_meta_row(db, &meta_row);
    if (status != SS_SDK_OK) {
        return status;
    }
    return ss_db_append_meta_if_changed(db, &loc, &meta_row);
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
        "ingested_utc INTEGER NOT NULL,"
        "CHECK(canonical >= 0),"
        "CHECK(value_type IN (0,1,2)),"
        "CHECK(data_kind IN (0,1)),"
        "CHECK(ingested_utc >= 0),"
        "CHECK(ts_start_utc >= 0),"
        "CHECK(ts_start_utc % 900 = 0),"
        "CHECK(ts_end_utc = ts_start_utc + 900),"
        "CHECK((value_type = 0 AND value_i64 IS NOT NULL AND typeof(value_i64) = 'integer' AND value_f64 IS NULL AND value_bool IS NULL)"
        "   OR (value_type = 1 AND value_f64 IS NOT NULL AND (typeof(value_f64) = 'real' OR typeof(value_f64) = 'integer') AND value_i64 IS NULL AND value_bool IS NULL)"
        "   OR (value_type = 2 AND value_bool IN (0,1) AND typeof(value_bool) = 'integer' AND value_i64 IS NULL AND value_f64 IS NULL)),"
        "UNIQUE(canonical, data_kind, ts_start_utc, ingested_utc)"
        ") STRICT;"
        "CREATE INDEX IF NOT EXISTS idx_records_canonical_data_kind_ts "
        "ON records(canonical, data_kind, ts_start_utc);"
        "CREATE INDEX IF NOT EXISTS idx_records_canonical_ts_kind_ingested "
        "ON records(canonical, ts_start_utc, data_kind, ingested_utc DESC);"
        "CREATE INDEX IF NOT EXISTS idx_records_canonical_ts "
        "ON records(canonical, ts_start_utc);";
    return ss_exec(db, k_canonical_data_schema_sql);
}

static void ss_db_close_locked(void)
{
    if (g_db != NULL) {
        int sqlite_result;

        (void)sqlite3_wal_checkpoint_v2(g_db, NULL, SQLITE_CHECKPOINT_PASSIVE, NULL, NULL);
#ifdef SS_SDK_ENABLE_TEST_HOOKS
        if (ss_test_consume(&g_db_test_hooks.fail_sqlite_close)) {
            sqlite_result = SQLITE_BUSY;
        } else {
            sqlite_result = sqlite3_close(g_db);
        }
#else
        sqlite_result = sqlite3_close(g_db);
#endif
        if (sqlite_result != SQLITE_OK) {
            (void)sqlite3_close_v2(g_db);
        }
        g_db = NULL;
    }

    free(g_db_open_path);
    g_db_open_path = NULL;
}

static ss_sdk_status ss_db_open_sqlite_handle(const char *path, sqlite3 **out_db)
{
    int sqlite_result;
    sqlite3 *opened_db = NULL;

    if (path == NULL || out_db == NULL) {
        return SS_SDK_ERR_INVALID_ARG;
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

    *out_db = opened_db;
    return SS_SDK_OK;
}

static ss_sdk_status ss_db_initialize_open_handle(sqlite3 *opened_db)
{
    ss_sdk_status status;

    if (opened_db == NULL) {
        return SS_SDK_ERR_INVALID_ARG;
    }

    status = ss_db_apply_pragmas(opened_db);
    if (status != SS_SDK_OK) {
        return status;
    }

    status = ss_db_create_canonical_data_schema(opened_db);
    if (status != SS_SDK_OK) {
        return status;
    }

    return ss_db_sync_location_metadata(opened_db);
}

static ss_sdk_status ss_db_adopt_open_handle(sqlite3 *opened_db, const char *path)
{
    char *opened_path;

    if (opened_db == NULL || path == NULL || path[0] == '\0') {
        return SS_SDK_ERR_INVALID_ARG;
    }

    opened_path = ss_strdup_local(path);
    if (opened_path == NULL) {
        return SS_SDK_ERR_INTERNAL;
    }

    g_db = opened_db;
    g_db_open_path = opened_path;
    return SS_SDK_OK;
}

static ss_sdk_status ss_db_open_locked(void)
{
    const char *path = ss_db_path();
    sqlite3 *opened_db = NULL;
    ss_sdk_status status;
    bool db_is_open;
    bool paths_match;

    if (path == NULL || path[0] == '\0') {
        return SS_SDK_ERR_INTERNAL;
    }

    db_is_open = (g_db != NULL);
    paths_match = db_is_open &&
                 g_db_open_path != NULL &&
                 strcmp(g_db_open_path, path) == 0;

    ss_db_debug_log_path("sdk.db.path.selected", path);

    // 1) Already open on requested path: reuse existing handle.
    if (paths_match) {
        ss_db_debug_log("sdk.db.reuse_open_handle", "reusing already open sqlite handle");
        return SS_SDK_OK;
    }

    // 2) Open on a different path: close old handle before switching.
    if (db_is_open) {
        ss_db_debug_log("sdk.db.switch_path", "closing existing sqlite handle before path switch");
        ss_db_close_locked();
    }

    // 3) First open or switched path: ensure parent folders exist.
    if (ss_ensure_parent_dirs(path) != 0) {
        return SS_SDK_ERR_INTERNAL;
    }

    status = ss_db_open_sqlite_handle(path, &opened_db);
    if (status != SS_SDK_OK) {
        return status;
    }

    status = ss_db_initialize_open_handle(opened_db);
    if (status != SS_SDK_OK) {
        (void)sqlite3_close(opened_db);
        return status;
    }

    status = ss_db_adopt_open_handle(opened_db, path);
    if (status != SS_SDK_OK) {
        (void)sqlite3_close(opened_db);
        return status;
    }
    ss_db_debug_log_path("sdk.db.opened", path);
    return SS_SDK_OK;
}

static ss_db_write_result ss_db_prepare_insert_stmt(sqlite3_stmt **out_stmt)
{
    const char *sql_text = ss_db_sql_text(SS_DB_SQL_INSERT_RECORD);
    ss_db_write_result result = {SS_SDK_ERR_INTERNAL, NULL, NULL};
    int sqlite_result;

    if (out_stmt == NULL || sql_text == NULL) {
        return result;
    }

    sqlite_result = sqlite3_prepare_v2(g_db, sql_text, -1, out_stmt, NULL);
#ifdef SS_SDK_ENABLE_TEST_HOOKS
    if (ss_test_consume(&g_db_test_hooks.fail_sqlite_prepare)) {
        sqlite_result = SQLITE_ERROR;
    }
#endif
    if (sqlite_result != SQLITE_OK || *out_stmt == NULL) {
        result.log_event = "sdk.db.prepare_failed";
        result.log_message = "sqlite prepare failed for insert statement";
        return result;
    }

    result.status = SS_SDK_OK;
    return result;
}

static int ss_db_bind_value_columns(sqlite3_stmt *stmt, const ss_sdk_record *record)
{
    int bind_error_count = 0;

    if (stmt == NULL || record == NULL) {
        return 1;
    }

    if (record->value_type == SS_SDK_VALUE_I64) {
        bind_error_count += (sqlite3_bind_int64(stmt, 3, (sqlite3_int64)record->value.i64) != SQLITE_OK);
        bind_error_count += (sqlite3_bind_null(stmt, 4) != SQLITE_OK);
        bind_error_count += (sqlite3_bind_null(stmt, 5) != SQLITE_OK);
    } else if (record->value_type == SS_SDK_VALUE_F64) {
        bind_error_count += (sqlite3_bind_null(stmt, 3) != SQLITE_OK);
        bind_error_count += (sqlite3_bind_double(stmt, 4, record->value.f64) != SQLITE_OK);
        bind_error_count += (sqlite3_bind_null(stmt, 5) != SQLITE_OK);
    } else if (record->value_type == SS_SDK_VALUE_BOOL) {
        bind_error_count += (sqlite3_bind_null(stmt, 3) != SQLITE_OK);
        bind_error_count += (sqlite3_bind_null(stmt, 4) != SQLITE_OK);
        bind_error_count += (sqlite3_bind_int(stmt, 5, record->value.boolean ? 1 : 0) != SQLITE_OK);
    } else {
        return -1;
    }

    return bind_error_count;
}

static ss_db_write_result ss_db_bind_insert_record(sqlite3_stmt *stmt, const ss_sdk_record *record)
{
    ss_db_write_result result = {SS_SDK_ERR_INTERNAL, NULL, NULL};
    int bind_error_count = 0;
    int value_bind_status;

    if (stmt == NULL || record == NULL) {
        return result;
    }

    bind_error_count += (sqlite3_bind_int(stmt, 1, (int)record->metric) != SQLITE_OK);
    bind_error_count += (sqlite3_bind_int(stmt, 2, (int)record->value_type) != SQLITE_OK);

    value_bind_status = ss_db_bind_value_columns(stmt, record);
    if (value_bind_status < 0) {
        result.status = SS_SDK_ERR_VALIDATION;
        result.log_event = "sdk.db.bind_invalid_type";
        result.log_message = "record value_type was invalid during bind";
        return result;
    }
    bind_error_count += value_bind_status;

    bind_error_count += (sqlite3_bind_int64(stmt, 6, (sqlite3_int64)record->ts_start_utc) != SQLITE_OK);
    bind_error_count += (sqlite3_bind_int64(stmt, 7, (sqlite3_int64)record->ts_end_utc) != SQLITE_OK);
    bind_error_count += (sqlite3_bind_int(stmt, 8, (int)record->data_kind) != SQLITE_OK);
    bind_error_count +=
        (sqlite3_bind_int64(
             stmt,
             9,
             (sqlite3_int64)(record->ingested_utc > 0 ? record->ingested_utc :
                             (record->data_kind == SS_SDK_DATA_OBSERVATION ? 0 : time(NULL)))) != SQLITE_OK);

    if (bind_error_count != 0) {
        result.log_event = "sdk.db.bind_failed";
        result.log_message = "sqlite bind failed for insert statement";
        return result;
    }

    result.status = SS_SDK_OK;
    return result;
}

static ss_db_write_result ss_db_execute_insert_step(sqlite3_stmt *stmt)
{
    ss_db_write_result result = {SS_SDK_ERR_INTERNAL, NULL, NULL};
    int sqlite_result;

    if (stmt == NULL) {
        return result;
    }

    sqlite_result = sqlite3_step(stmt);
#ifdef SS_SDK_ENABLE_TEST_HOOKS
    if (ss_test_consume(&g_db_test_hooks.fail_sqlite_step)) {
        sqlite_result = SQLITE_ERROR;
    }
#endif

    if (sqlite_result == SQLITE_DONE) {
        if (sqlite3_changes(g_db) == 0) {
            result.log_event = "sdk.db.dedupe_drop";
            result.log_message = "duplicate canonical slot/release ignored";
        }
        result.status = SS_SDK_OK;
        return result;
    }
    if (sqlite_result == SQLITE_CONSTRAINT) {
        result.status = SS_SDK_ERR_VALIDATION;
        result.log_event = "sdk.db.step_constraint";
        result.log_message = "sqlite constraint rejected insert";
        return result;
    }

    result.status = SS_SDK_ERR_INTERNAL;
    result.log_event = "sdk.db.step_internal_error";
    result.log_message = "sqlite step failed during insert";
    return result;
}

static void ss_db_log_write_result(const ss_db_write_result *result)
{
    if (result == NULL || result->log_event == NULL || result->log_message == NULL) {
        return;
    }

    if (strcmp(result->log_event, "sdk.db.dedupe_drop") == 0) {
        SS_LOG_DEBUG(result->log_event, result->log_message);
        return;
    }
    SS_LOG_ERROR(result->log_event, result->log_message);
}

ss_sdk_status ss_sdk_internal_db_write_record(const ss_sdk_record *record)
{
    sqlite3_stmt *insert_statment = NULL;
    ss_db_write_result write_result = {SS_SDK_ERR_INTERNAL, NULL, NULL};

    if (record == NULL) {
        return SS_SDK_ERR_INVALID_ARG;
    }

    ss_db_debug_log("sdk.db.write.begin", "internal db write started");

    pthread_mutex_lock(&g_db_mu);

    write_result.status = ss_db_open_locked();
    if (write_result.status != SS_SDK_OK) {
        write_result.log_event = "sdk.db.open_failed";
        write_result.log_message = "failed to open sqlite connection for write";
        goto cleanup;
    }

    write_result = ss_db_prepare_insert_stmt(&insert_statment);
    if (write_result.status != SS_SDK_OK) {
        goto cleanup;
    }

    write_result = ss_db_bind_insert_record(insert_statment, record);
    if (write_result.status != SS_SDK_OK) {
        goto cleanup;
    }

    write_result = ss_db_execute_insert_step(insert_statment);

cleanup:
    if (insert_statment != NULL) {
        sqlite3_finalize(insert_statment);
    }
    pthread_mutex_unlock(&g_db_mu);
    ss_db_log_write_result(&write_result);
    return write_result.status;
}

ss_sdk_status ss_sdk_internal_db_write_records(
    const ss_sdk_record *records,
    size_t count,
    size_t *out_written)
{
    sqlite3_stmt *insert_statement = NULL;
    ss_db_write_result write_result = {SS_SDK_ERR_INTERNAL, NULL, NULL};
    ss_sdk_status status = SS_SDK_ERR_INTERNAL;
    size_t inserted_rows = 0U;
    size_t i;

    if (records == NULL || out_written == NULL || count == 0U) {
        return SS_SDK_ERR_INVALID_ARG;
    }
    *out_written = 0U;

    pthread_mutex_lock(&g_db_mu);

    status = ss_db_open_locked();
    if (status != SS_SDK_OK) {
        goto cleanup;
    }

    status = ss_exec(g_db, "BEGIN IMMEDIATE;");
    if (status != SS_SDK_OK) {
        goto cleanup;
    }

    write_result = ss_db_prepare_insert_stmt(&insert_statement);
    if (write_result.status != SS_SDK_OK) {
        status = write_result.status;
        goto rollback;
    }

    for (i = 0U; i < count; ++i) {
        write_result = ss_db_bind_insert_record(insert_statement, &records[i]);
        if (write_result.status != SS_SDK_OK) {
            status = write_result.status;
            goto rollback;
        }

        write_result = ss_db_execute_insert_step(insert_statement);
        if (write_result.status != SS_SDK_OK) {
            status = write_result.status;
            goto rollback;
        }
        if (!(write_result.log_event != NULL && strcmp(write_result.log_event, "sdk.db.dedupe_drop") == 0)) {
            inserted_rows += 1U;
        }

        if (sqlite3_reset(insert_statement) != SQLITE_OK) {
            status = SS_SDK_ERR_INTERNAL;
            goto rollback;
        }
        (void)sqlite3_clear_bindings(insert_statement);
    }

    status = ss_exec(g_db, "COMMIT;");
    if (status == SS_SDK_OK) {
        *out_written = inserted_rows;
        goto cleanup;
    }

rollback:
    (void)ss_exec(g_db, "ROLLBACK;");
    ss_db_log_write_result(&write_result);

cleanup:
    if (insert_statement != NULL) {
        sqlite3_finalize(insert_statement);
    }
    pthread_mutex_unlock(&g_db_mu);
    return status;
}

static bool ss_select_exact_row_for_slot(
    const ss_raw_row *rows,
    size_t row_count,
    int64_t ts_utc,
    bool is_future,
    ss_interp_policy policy,
    ss_raw_row *out_row,
    ss_sdk_sample_flags *out_flags)
{
    if (rows == NULL || out_row == NULL || out_flags == NULL) {
        return false;
    }

    if (!is_future) {
        if (ss_find_exact_row(rows, row_count, ts_utc, SS_SDK_DATA_OBSERVATION, out_row)) {
            *out_flags = SS_SDK_SAMPLE_OBSERVED;
            return true;
        }
        if (ss_find_exact_row(rows, row_count, ts_utc, SS_SDK_DATA_FORECAST, out_row)) {
            *out_flags = SS_SDK_SAMPLE_FORECAST;
            return true;
        }
        return false;
    }

    if (ss_find_exact_row(rows, row_count, ts_utc, SS_SDK_DATA_FORECAST, out_row)) {
        *out_flags = SS_SDK_SAMPLE_FORECAST;
        return true;
    }

    if (policy == SS_INTERP_POLICY_STEP &&
        ss_find_exact_row(rows, row_count, ts_utc, SS_SDK_DATA_OBSERVATION, out_row)) {
        *out_flags = SS_SDK_SAMPLE_OBSERVED;
        return true;
    }

    return false;
}

static bool ss_fill_interpolated_row_for_slot(
    ss_metric_id canonical,
    ss_sdk_value_type value_type,
    const ss_raw_row *rows,
    size_t row_count,
    int64_t ts_utc,
    bool is_future,
    ss_interp_policy policy,
    ss_raw_row *out_row,
    ss_sdk_sample_flags *out_flags,
    bool *out_interp_too_long)
{
    ss_sdk_value interpolated;
    bool allow_obs_interp;
    bool allow_fc_interp = true;

    if (out_row == NULL || out_flags == NULL || out_interp_too_long == NULL) {
        return false;
    }
    *out_interp_too_long = false;

    allow_obs_interp = (!is_future) || (policy == SS_INTERP_POLICY_STEP);
    if (!ss_try_interpolate(
            canonical,
            value_type,
            rows,
            row_count,
            ts_utc,
            allow_obs_interp,
            allow_fc_interp,
            out_interp_too_long,
            &interpolated)) {
        return false;
    }

    out_row->ts_utc = ts_utc;
    out_row->data_kind = SS_SDK_DATA_FORECAST;
    out_row->value_type = value_type;
    out_row->value = interpolated;
    *out_flags = SS_SDK_SAMPLE_INTERPOLATED;
    return true;
}

static void ss_sample_write_slot(
    ss_sdk_sample *dst,
    ss_metric_id canonical,
    ss_sdk_value_type value_type,
    int64_t ts_utc,
    const ss_raw_row *row,
    ss_sdk_sample_flags flags)
{
    dst->ts_utc = ts_utc;
    dst->canonical = canonical;
    dst->value_type = value_type;
    dst->value = row->value;
    dst->flags = flags;
}

static ss_sdk_status ss_db_load_window_rows(
    ss_metric_id canonical,
    ss_sdk_value_type value_type,
    int64_t start_utc,
    int64_t end_utc,
    ss_raw_row **out_rows,
    size_t *out_count)
{
    ss_sdk_status status;
    if (out_rows == NULL || out_count == NULL) {
        return SS_SDK_ERR_INVALID_ARG;
    }

    *out_rows = NULL;
    *out_count = 0U;

    pthread_mutex_lock(&g_db_mu);
    status = ss_db_open_locked();
    if (status == SS_SDK_OK) {
        status = ss_load_rows_for_window(g_db, canonical, value_type, start_utc, end_utc, out_rows, out_count);
    }
    pthread_mutex_unlock(&g_db_mu);
    return status;
}

static ss_sdk_status ss_alloc_samples_buffer(size_t expected_slots, ss_sdk_sample **out_samples)
{
    ss_sdk_sample *samples = NULL;
    if (out_samples == NULL) {
        return SS_SDK_ERR_INVALID_ARG;
    }
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
    *out_samples = samples;
    return SS_SDK_OK;
}

static ss_sdk_status ss_process_slot(
    ss_metric_id canonical,
    ss_sdk_value_type value_type,
    const ss_raw_row *window_rows,
    size_t window_row_count,
    int64_t now_slot,
    int64_t ts_utc,
    ss_interp_policy policy,
    ss_raw_row *out_row,
    ss_sdk_sample_flags *out_flags,
    bool *out_have_value)
{
    bool is_future;
    bool have_value;
    bool interpolation_length_exceeded = false;

    if (out_row == NULL || out_flags == NULL || out_have_value == NULL) {
        return SS_SDK_ERR_INVALID_ARG;
    }
    *out_have_value = false;

    is_future = (ts_utc > now_slot);
    have_value = ss_select_exact_row_for_slot(window_rows, window_row_count, ts_utc, is_future, policy, out_row, out_flags);

    if (!have_value) {
        have_value = ss_fill_interpolated_row_for_slot(
            canonical,
            value_type,
            window_rows,
            window_row_count,
            ts_utc,
            is_future,
            policy,
            out_row,
            out_flags,
            &interpolation_length_exceeded);
    }

    if (!have_value && interpolation_length_exceeded) {
        return SS_SDK_ERR_PARTIAL_DATA;
    }
    *out_have_value = have_value;
    return SS_SDK_OK;
}

static ss_sdk_status ss_finalize_samples_out(
    ss_sdk_sample *samples,
    size_t sample_count,
    size_t expected_slots,
    ss_sdk_samples_out *out)
{
    if (samples == NULL || out == NULL) {
        return SS_SDK_ERR_INVALID_ARG;
    }

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

typedef struct ss_canonical_query_ctx {
    const ss_metric_meta *metric_metadata;
    ss_interp_policy policy;
    size_t expected_slots;
    ss_raw_row *window_rows;
    size_t window_row_count;
    ss_sdk_sample *samples;
} ss_canonical_query_ctx;

static bool ss_interp_policy_map_complete_cached(void)
{
    return ss_all_metrics_have_interpolation_policy();
}

static ss_sdk_status ss_validate_canonical_query_args(
    int64_t start_utc,
    int64_t end_utc,
    ss_sdk_samples_out *out)
{
    if (out == NULL) {
        return SS_SDK_ERR_INVALID_ARG;
    }

    out->samples = NULL;
    out->count = 0;
    if (start_utc < 0 || end_utc <= start_utc || ((end_utc - start_utc) % SS_SLOT_SECONDS) != 0) {
        return SS_SDK_ERR_INVALID_ARG;
    }
    return SS_SDK_OK;
}

static ss_sdk_status ss_prepare_canonical_query_ctx(
    ss_metric_id canonical,
    int64_t start_utc,
    int64_t end_utc,
    ss_canonical_query_ctx *ctx)
{
    ss_sdk_status status;
    if (ctx == NULL) {
        return SS_SDK_ERR_INVALID_ARG;
    }

    memset(ctx, 0, sizeof(*ctx));

    ctx->metric_metadata = ss_metric_meta_get(canonical);
    if (ctx->metric_metadata == NULL || !ss_value_type_is_supported(ctx->metric_metadata->value_type)) {
        return SS_SDK_ERR_INVALID_ARG;
    }
    if (!ss_interp_policy_map_complete_cached()) {
        return SS_SDK_ERR_INTERNAL;
    }
    ctx->policy = ss_interpolation_policy(canonical);
    if (ctx->policy == SS_INTERP_POLICY_INVALID) {
        return SS_SDK_ERR_INTERNAL;
    }

    ctx->expected_slots = (size_t)((end_utc - start_utc) / SS_SLOT_SECONDS);
    status = ss_alloc_samples_buffer(ctx->expected_slots, &ctx->samples);
    if (status != SS_SDK_OK) {
        return status;
    }

    status = ss_db_load_window_rows(
        canonical,
        ctx->metric_metadata->value_type,
        start_utc,
        end_utc,
        &ctx->window_rows,
        &ctx->window_row_count);
    if (status != SS_SDK_OK) {
        free(ctx->samples);
        ctx->samples = NULL;
        return status;
    }

    return SS_SDK_OK;
}

static ss_sdk_status ss_collect_canonical_samples(
    ss_metric_id canonical,
    int64_t start_utc,
    int64_t end_utc,
    const ss_canonical_query_ctx *ctx,
    size_t *out_sample_count)
{
    int64_t now_slot;
    int64_t ts_utc;
    size_t sample_count = 0U;

    if (ctx == NULL || out_sample_count == NULL) {
        return SS_SDK_ERR_INVALID_ARG;
    }
    *out_sample_count = 0U;

    now_slot = ss_now_slot_utc();
    for (ts_utc = start_utc; ts_utc < end_utc; ts_utc += SS_SLOT_SECONDS) {
        ss_raw_row selected_row;
        bool have_value = false;
        ss_sdk_sample_flags sample_flags = 0;
        ss_sdk_status slot_status;

        slot_status = ss_process_slot(
            canonical,
            ctx->metric_metadata->value_type,
            ctx->window_rows,
            ctx->window_row_count,
            now_slot,
            ts_utc,
            ctx->policy,
            &selected_row,
            &sample_flags,
            &have_value);
        if (slot_status == SS_SDK_ERR_PARTIAL_DATA) {
            return SS_SDK_ERR_PARTIAL_DATA;
        }
        if (!have_value) {
            continue;
        }

        ss_sample_write_slot(
            &ctx->samples[sample_count],
            canonical,
            ctx->metric_metadata->value_type,
            ts_utc,
            &selected_row,
            sample_flags);
        sample_count += 1U;
    }

    *out_sample_count = sample_count;
    return SS_SDK_OK;
}

ss_sdk_status ss_sdk_internal_db_get_canonical(
    ss_metric_id canonical,
    int64_t start_utc,
    int64_t end_utc,
    ss_sdk_samples_out *out)
{
    ss_canonical_query_ctx ctx;
    size_t sample_count = 0;
    ss_sdk_status status;

    ss_db_debug_log("sdk.db.get.begin", "internal canonical range read started");

    status = ss_validate_canonical_query_args(start_utc, end_utc, out);
    if (status != SS_SDK_OK) {
        return status;
    }

    status = ss_prepare_canonical_query_ctx(canonical, start_utc, end_utc, &ctx);
    if (status != SS_SDK_OK) {
        return status;
    }

    status = ss_collect_canonical_samples(canonical, start_utc, end_utc, &ctx, &sample_count);
    free(ctx.window_rows);
    if (status != SS_SDK_OK) {
        free(ctx.samples);
        out->samples = NULL;
        out->count = 0;
        return status;
    }

    return ss_finalize_samples_out(ctx.samples, sample_count, ctx.expected_slots, out);
}

static ss_sdk_status ss_db_select_max_ts_from_start(
    ss_metric_id canonical,
    int64_t start_utc,
    int64_t *out_max_ts_start,
    bool *out_has_value)
{
    const char *sql_text = ss_db_sql_text(SS_DB_SQL_SELECT_MAX_TS_FROM_START);
    sqlite3_stmt *max_statement = NULL;
    int sqlite_result;
    ss_sdk_status status;

    if (out_max_ts_start == NULL || out_has_value == NULL) {
        return SS_SDK_ERR_INVALID_ARG;
    }
    *out_max_ts_start = 0;
    *out_has_value = false;

    pthread_mutex_lock(&g_db_mu);
    status = ss_db_open_locked();
    if (status != SS_SDK_OK) {
        pthread_mutex_unlock(&g_db_mu);
        return status;
    }

    sqlite_result = sqlite3_prepare_v2(g_db, sql_text, -1, &max_statement, NULL);
    if (sqlite_result != SQLITE_OK) {
        pthread_mutex_unlock(&g_db_mu);
        return SS_SDK_ERR_INTERNAL;
    }

    sqlite3_bind_int(max_statement, 1, (int)canonical);
    sqlite3_bind_int64(max_statement, 2, (sqlite3_int64)start_utc);

    sqlite_result = sqlite3_step(max_statement);
    if (sqlite_result == SQLITE_ROW) {
        if (sqlite3_column_type(max_statement, 0) != SQLITE_NULL) {
            *out_max_ts_start = (int64_t)sqlite3_column_int64(max_statement, 0);
            *out_has_value = true;
        }
    } else if (sqlite_result != SQLITE_DONE) {
        sqlite3_finalize(max_statement);
        pthread_mutex_unlock(&g_db_mu);
        return SS_SDK_ERR_INTERNAL;
    }

    sqlite3_finalize(max_statement);
    pthread_mutex_unlock(&g_db_mu);
    return SS_SDK_OK;
}

ss_sdk_status ss_sdk_internal_db_get_canonical_forward(
    ss_metric_id canonical,
    int64_t start_utc,
    ss_sdk_samples_out *out)
{
    bool has_value = false;
    int64_t max_ts_start = 0;
    int64_t end_utc;
    ss_sdk_status status;

    if (out == NULL || start_utc < 0) {
        return SS_SDK_ERR_INVALID_ARG;
    }
    out->samples = NULL;
    out->count = 0;

    status = ss_db_select_max_ts_from_start(canonical, start_utc, &max_ts_start, &has_value);
    if (status != SS_SDK_OK) {
        return status;
    }

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
