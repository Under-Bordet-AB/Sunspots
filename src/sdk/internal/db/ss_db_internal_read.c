#include "sdk/internal/db/ss_db_internal_shared.h"

#include <errno.h>
#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <time.h>

bool ss_value_type_is_supported(ss_sdk_value_type value_type)
{
    return value_type == SS_SDK_VALUE_I64 ||
           value_type == SS_SDK_VALUE_F64 ||
           value_type == SS_SDK_VALUE_BOOL;
}

// Not all canonicals are interpolated the same way. Price data should never interpolate for example.
ss_interp_policy ss_interpolation_policy(ss_metric_id canonical)
{
    switch (canonical) {
        case SS_METRIC_WEATHER_TEMPERATURE_AIR_2M_C:
        case SS_METRIC_WEATHER_HUMIDITY_RELATIVE_2M_PCT:
        case SS_METRIC_WEATHER_WIND_SPEED_10M_MS:
        case SS_METRIC_WEATHER_WIND_GUST_10M_MS:
        case SS_METRIC_WEATHER_WIND_DIRECTION_10M_DEG:
        case SS_METRIC_WEATHER_PRESSURE_MSL_HPA:
        case SS_METRIC_WEATHER_VISIBILITY_KM:
        case SS_METRIC_WEATHER_CLOUD_COVER_TOTAL_PCT:
        case SS_METRIC_WEATHER_CLOUD_COVER_LOW_PCT:
        case SS_METRIC_WEATHER_CLOUD_COVER_MID_PCT:
        case SS_METRIC_WEATHER_CLOUD_COVER_HIGH_PCT:
        case SS_METRIC_WEATHER_PRECIP_AMOUNT_MM:
        case SS_METRIC_WEATHER_PRECIP_PROBABILITY_PCT:
        case SS_METRIC_WEATHER_RADIATION_SHORTWAVE_WM2:
        case SS_METRIC_WEATHER_TEMPERATURE_DEW_POINT_C:
        case SS_METRIC_WEATHER_TEMPERATURE_APPARENT_C:
        case SS_METRIC_WEATHER_PRECIP_THUNDERSTORM_PCT:
        case SS_METRIC_WEATHER_FOG_PROBABILITY_PCT:
        case SS_METRIC_ENERGY_FX_SEK_PER_EUR:
            return SS_INTERP_POLICY_LINEAR;

        case SS_METRIC_ENERGY_PRICE_SPOT_SEK_KWH:
        case SS_METRIC_ENERGY_PRICE_SPOT_EUR_KWH:
            return SS_INTERP_POLICY_STEP;

        case SS_METRIC_WEATHER_CONDITION_SYMBOL_CODE:
        case SS_METRIC_WEATHER_IS_DAY:
            return SS_INTERP_POLICY_NONE;

        default:
            return SS_INTERP_POLICY_INVALID;
    }
}

bool ss_all_metrics_have_interpolation_policy(void)
{
    int id;

    for (id = 0; id < (int)SS_METRIC_COUNT; ++id) {
        if (ss_interpolation_policy((ss_metric_id)id) == SS_INTERP_POLICY_INVALID
#ifdef SS_SDK_ENABLE_TEST_HOOKS
            || ss_test_consume(&g_db_test_hooks.force_interp_incomplete)
#endif
        ) {
            return false;
        }
    }

    return true;
}

// align start time to SS_SLOT_SECONDS size
int64_t ss_now_slot_utc(void)
{
    int64_t now_utc;

#ifdef SS_SDK_ENABLE_TEST_HOOKS
    if (ss_test_consume(&g_db_test_hooks.force_now_negative)) {
        now_utc = -1;
    } else {
        now_utc = (int64_t)time(NULL);
    }
#else
    now_utc = (int64_t)time(NULL);
#endif

    if (now_utc < 0) {
        return 0;
    }

    return now_utc - (now_utc % SS_SLOT_SECONDS);
}

// Grow raw output data array dynamically (double if full, bootstrap to 64 rows if empty)
bool ss_raw_rows_append(ss_raw_row **rows, size_t *count, size_t *cap, const ss_raw_row *row)
{
    ss_raw_row *grown;
    size_t next_cap;

    if (*count == *cap) {
        next_cap = (*cap == 0U) ? 64U : (*cap * 2U);
        if (next_cap < *cap || next_cap > SIZE_MAX / sizeof(ss_raw_row)) {
            return false;
        }

        grown = (ss_raw_row *)realloc(*rows, next_cap * sizeof(ss_raw_row));
#ifdef SS_SDK_ENABLE_TEST_HOOKS
        if (ss_test_consume(&g_db_test_hooks.fail_realloc)) {
            free(grown);
            grown = NULL;
            errno = ENOMEM;
        }
#endif
        if (grown == NULL) {
            return false;
        }

        *rows = grown;
        *cap = next_cap;
    }

    (*rows)[*count] = *row;
    *count += 1U;
    return true;
}

// Raw row lookup helpers used by slot selection and interpolation.
static size_t ss_lower_bound_ts(const ss_raw_row *rows, size_t count, int64_t ts_utc)
{
    size_t left = 0U;
    size_t right = count;

    while (left < right) {
        size_t mid = left + (right - left) / 2U;
        if (rows[mid].ts_utc < ts_utc) {
            left = mid + 1U;
        } else {
            right = mid;
        }
    }
    return left;
}

static size_t ss_upper_bound_ts(const ss_raw_row *rows, size_t count, int64_t ts_utc)
{
    size_t left = 0U;
    size_t right = count;

    while (left < right) {
        size_t mid = left + (right - left) / 2U;
        if (rows[mid].ts_utc <= ts_utc) {
            left = mid + 1U;
        } else {
            right = mid;
        }
    }
    return left;
}

bool ss_find_exact_row(
    const ss_raw_row *rows,
    size_t count,
    int64_t ts_utc,
    ss_sdk_data_kind data_kind,
    ss_raw_row *out)
{
    size_t i;
    size_t first;

    if (rows == NULL || out == NULL || count == 0U) {
        return false;
    }

    first = ss_lower_bound_ts(rows, count, ts_utc);
    for (i = first; i < count && rows[i].ts_utc == ts_utc; ++i) {
        if (rows[i].ts_utc == ts_utc && rows[i].data_kind == data_kind) {
            *out = rows[i];
            return true;
        }
    }

    return false;
}

static bool ss_data_kind_is_allowed(ss_sdk_data_kind data_kind, bool allow_observation, bool allow_forecast)
{
    if (data_kind == SS_SDK_DATA_OBSERVATION) {
        return allow_observation;
    }
    if (data_kind == SS_SDK_DATA_FORECAST) {
        return allow_forecast;
    }
    return false;
}

// Find the latest row at or before ts_utc.
static bool ss_find_prev_row(
    const ss_raw_row *rows,
    size_t count,
    int64_t ts_utc,
    bool allow_observation,
    bool allow_forecast,
    ss_raw_row *out)
{
    size_t i;
    int64_t candidate_ts = -1;
    bool have_candidate = false;
    ss_raw_row candidate = {0};

    if (rows == NULL || out == NULL || count == 0U) {
        return false;
    }

    i = ss_upper_bound_ts(rows, count, ts_utc);
    while (i > 0U) {
        i -= 1U;
        if (!ss_data_kind_is_allowed(rows[i].data_kind, allow_observation, allow_forecast)) {
            continue;
        }
        if (!have_candidate) {
            candidate = rows[i];
            candidate_ts = rows[i].ts_utc;
            have_candidate = true;
            continue;
        }
        if (rows[i].ts_utc == candidate_ts) {
            // Rows are sorted newest-first inside each slot, so keep walking
            // to end up on the newest entry when scanning backwards.
            candidate = rows[i];
            continue;
        }
        break;
    }

    if (have_candidate) {
        *out = candidate;
        return true;
    }

    return false;
}

// Find nearest rows strictly before and strictly after ts_utc.
static bool ss_find_prev_next_rows(
    const ss_raw_row *rows,
    size_t count,
    int64_t ts_utc,
    bool allow_observation,
    bool allow_forecast,
    ss_raw_row *out_prev,
    ss_raw_row *out_next)
{
    size_t i;
    size_t first_gt;
    bool have_prev = false;
    bool have_next = false;

    if (rows == NULL || out_prev == NULL || out_next == NULL || count == 0U) {
        return false;
    }

    first_gt = ss_upper_bound_ts(rows, count, ts_utc);

    i = first_gt;
    while (i > 0U) {
        int64_t candidate_ts = -1;
        bool have_candidate = false;
        ss_raw_row candidate = {0};
        i -= 1U;
        while (true) {
            if (ss_data_kind_is_allowed(rows[i].data_kind, allow_observation, allow_forecast) &&
                rows[i].ts_utc < ts_utc) {
                if (!have_candidate) {
                    candidate = rows[i];
                    candidate_ts = rows[i].ts_utc;
                    have_candidate = true;
                } else if (rows[i].ts_utc == candidate_ts) {
                    candidate = rows[i];
                } else {
                    break;
                }
            }

            if (i == 0U) {
                break;
            }
            if (rows[i - 1U].ts_utc != rows[i].ts_utc) {
                break;
            }
            i -= 1U;
        }

        if (have_candidate) {
            *out_prev = candidate;
            have_prev = true;
            break;
        }
    }

    for (i = first_gt; i < count; ++i) {
        if (!ss_data_kind_is_allowed(rows[i].data_kind, allow_observation, allow_forecast)) {
            continue;
        }
        if (rows[i].ts_utc > ts_utc) {
            *out_next = rows[i];
            have_next = true;
            break;
        }
    }

    return have_prev && have_next;
}

static void ss_mark_interp_too_long(bool *out_interpolation_length_exceeded)
{
    if (out_interpolation_length_exceeded != NULL) {
        *out_interpolation_length_exceeded = true;
    }
}

static bool ss_try_interpolate_step(
    const ss_raw_row *rows,
    size_t count,
    int64_t ts_utc,
    bool allow_observation,
    bool allow_forecast,
    bool *out_interpolation_length_exceeded,
    ss_sdk_value *out_value)
{
    ss_raw_row prev = {0};
    int64_t age_sec;

    if (!ss_find_prev_row(rows, count, ts_utc, allow_observation, allow_forecast, &prev)) {
        return false;
    }

    age_sec = ts_utc - prev.ts_utc;
    if (age_sec < 0 || age_sec > SS_DB_INTERP_MAX_LENGTH_SECONDS) {
        ss_mark_interp_too_long(out_interpolation_length_exceeded);
        return false;
    }

    *out_value = prev.value;
    return true;
}

static bool ss_try_interpolate_linear(
    ss_sdk_value_type value_type,
    const ss_raw_row *rows,
    size_t count,
    int64_t ts_utc,
    bool allow_observation,
    bool allow_forecast,
    bool *out_interpolation_length_exceeded,
    ss_sdk_value *out_value)
{
    ss_raw_row prev = {0};
    ss_raw_row next = {0};
    int64_t span_sec;
    int64_t delta_sec;
    double ratio;

    if (value_type != SS_SDK_VALUE_F64) {
        return false;
    }
    if (!ss_find_prev_next_rows(rows, count, ts_utc, allow_observation, allow_forecast, &prev, &next)) {
        return false;
    }

    span_sec = next.ts_utc - prev.ts_utc;
#ifdef SS_SDK_ENABLE_TEST_HOOKS
    if (ss_test_consume(&g_db_test_hooks.force_linear_zero_span)) {
        span_sec = 0;
    }
#endif
    if (span_sec <= 0) {
        return false;
    }
    if (span_sec > SS_DB_INTERP_MAX_LENGTH_SECONDS) {
        ss_mark_interp_too_long(out_interpolation_length_exceeded);
        return false;
    }

    delta_sec = ts_utc - prev.ts_utc;
    ratio = (double)delta_sec / (double)span_sec;
    out_value->f64 = prev.value.f64 + ratio * (next.value.f64 - prev.value.f64);
    return isfinite(out_value->f64);
}

bool ss_try_interpolate(
    ss_metric_id canonical,
    ss_sdk_value_type value_type,
    const ss_raw_row *rows,
    size_t count,
    int64_t ts_utc,
    bool allow_observation,
    bool allow_forecast,
    bool *out_interpolation_length_exceeded,
    ss_sdk_value *out_value)
{
    ss_interp_policy policy = ss_interpolation_policy(canonical);

    if (out_interpolation_length_exceeded != NULL) {
        *out_interpolation_length_exceeded = false;
    }

#ifdef SS_SDK_ENABLE_TEST_HOOKS
    if (ss_test_consume(&g_db_test_hooks.force_policy_unknown)) {
        policy = (ss_interp_policy)12345;
    }
#endif

    if (policy == SS_INTERP_POLICY_NONE || policy == SS_INTERP_POLICY_INVALID) {
        return false;
    }

    if (policy == SS_INTERP_POLICY_STEP) {
        return ss_try_interpolate_step(
            rows,
            count,
            ts_utc,
            allow_observation,
            allow_forecast,
            out_interpolation_length_exceeded,
            out_value);
    }

    if (policy == SS_INTERP_POLICY_LINEAR) {
        return ss_try_interpolate_linear(
            value_type,
            rows,
            count,
            ts_utc,
            allow_observation,
            allow_forecast,
            out_interpolation_length_exceeded,
            out_value);
    }

    return false;
}

static bool ss_decode_row_if_valid(sqlite3_stmt *query_statement, ss_sdk_value_type expected_type, ss_raw_row *out_row)
{
    int data_kind = sqlite3_column_int(query_statement, 1);
    int value_type = sqlite3_column_int(query_statement, 2);

    out_row->ts_utc = (int64_t)sqlite3_column_int64(query_statement, 0);
    if (out_row->ts_utc < 0 || out_row->ts_utc % SS_SLOT_SECONDS != 0) {
        return false;
    }
    if (data_kind != SS_SDK_DATA_OBSERVATION && data_kind != SS_SDK_DATA_FORECAST) {
        return false;
    }
    if ((ss_sdk_value_type)value_type != expected_type ||
        !ss_value_type_is_supported((ss_sdk_value_type)value_type)) {
        return false;
    }

    out_row->data_kind = (ss_sdk_data_kind)data_kind;
    out_row->value_type = (ss_sdk_value_type)value_type;

    if (out_row->value_type == SS_SDK_VALUE_I64) {
        if (sqlite3_column_type(query_statement, 3) == SQLITE_NULL) {
            return false;
        }
        out_row->value.i64 = (int64_t)sqlite3_column_int64(query_statement, 3);
        return true;
    }

    if (out_row->value_type == SS_SDK_VALUE_F64) {
        if (sqlite3_column_type(query_statement, 4) == SQLITE_NULL) {
            return false;
        }
        out_row->value.f64 = sqlite3_column_double(query_statement, 4);
        return isfinite(out_row->value.f64);
    }

    if (out_row->value_type == SS_SDK_VALUE_BOOL) {
        int value_bool;

        if (sqlite3_column_type(query_statement, 5) == SQLITE_NULL) {
            return false;
        }
        value_bool = sqlite3_column_int(query_statement, 5);
        if (value_bool != 0 && value_bool != 1) {
            return false;
        }
        out_row->value.boolean = (value_bool != 0);
        return true;
    }

    return false;
}

static void ss_compute_padded_query_bounds(int64_t start_utc, int64_t end_utc, int64_t *out_start, int64_t *out_end)
{
    const int64_t padding_seconds = (int64_t)SS_DB_INTERP_PADDING_SLOTS * (int64_t)SS_SLOT_SECONDS;

    if (start_utc < padding_seconds) {
        *out_start = 0;
    } else {
        *out_start = start_utc - padding_seconds;
    }

    if (end_utc > INT64_MAX - padding_seconds) {
        *out_end = INT64_MAX;
    } else {
        *out_end = end_utc + padding_seconds;
    }
}

ss_sdk_status ss_load_rows_for_window(
    sqlite3 *db,
    ss_metric_id canonical,
    ss_sdk_value_type expected_type,
    int64_t start_utc,
    int64_t end_utc,
    ss_raw_row **out_rows,
    size_t *out_count)
{
    sqlite3_stmt *query_statement = NULL;
    ss_sdk_status status = SS_SDK_ERR_INTERNAL;
    int sqlite_result;
    int64_t query_start;
    int64_t query_end;
    ss_raw_row *rows = NULL;
    size_t count = 0;
    size_t cap = 0;
    const char *sql_text = ss_db_sql_text(SS_DB_SQL_SELECT_ROWS_WINDOW);

    *out_rows = NULL;
    *out_count = 0;

    ss_compute_padded_query_bounds(start_utc, end_utc, &query_start, &query_end);

    sqlite_result = sqlite3_prepare_v2(db, sql_text, -1, &query_statement, NULL);
#ifdef SS_SDK_ENABLE_TEST_HOOKS
    if (ss_test_consume(&g_db_test_hooks.fail_sqlite_prepare)) {
        sqlite_result = SQLITE_ERROR;
    }
#endif
    if (sqlite_result != SQLITE_OK) {
        goto cleanup;
    }

    sqlite3_bind_int(query_statement, 1, (int)canonical);               // ?1 canonical metric id
    sqlite3_bind_int64(query_statement, 2, (sqlite3_int64)query_start); // ?2 query window start (UTC)
    sqlite3_bind_int64(query_statement, 3, (sqlite3_int64)query_end);   // ?3 query window end (UTC, exclusive)

    for (;;) {
        ss_raw_row row;

#ifdef SS_SDK_ENABLE_TEST_HOOKS
        if (ss_test_consume(&g_db_test_hooks.fail_sqlite_step)) {
            sqlite_result = SQLITE_ERROR;
        } else {
            sqlite_result = sqlite3_step(query_statement);
        }
#else
        sqlite_result = sqlite3_step(query_statement);
#endif
        if (sqlite_result == SQLITE_DONE) {
            status = SS_SDK_OK;
            break;
        }
        if (sqlite_result != SQLITE_ROW) {
            goto cleanup;
        }

        if (!ss_decode_row_if_valid(query_statement, expected_type, &row)) {
            continue;
        }

        if (!ss_raw_rows_append(&rows, &count, &cap, &row)) {
            goto cleanup;
        }
    }

cleanup:
    if (query_statement != NULL) {
        sqlite3_finalize(query_statement);
    }
    if (status != SS_SDK_OK) {
        free(rows);
        return status;
    }

    *out_rows = rows;
    *out_count = count;
    return status;
}
