#include "backfill_payload.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../sdk/ss_sdk.h"

typedef struct {
    int limit_to_window;
    int64_t window_start_utc;
    int64_t window_end_utc;
    int64_t edge_padding_sec;
    ss_sdk_data_kind data_kind;
    int64_t ingested_utc;
} payload_write_spec;

typedef struct {
    ss_sdk_record *records;
    size_t count;
    size_t cap;
} record_buffer;

typedef struct {
    int64_t ts_utc;
    double temp;
    double cloud;
    double rad;
} payload_metric_row;

static int archive_extract_arrays(cJSON *root, archive_hourly_arrays *arrays)
{
    cJSON *hourly = NULL;

    if (root == NULL || arrays == NULL) {
        return -1;
    }

    memset(arrays, 0, sizeof(*arrays));
    hourly = cJSON_GetObjectItemCaseSensitive(root, "hourly");
    if (!cJSON_IsObject(hourly)) {
        return -1;
    }

    arrays->times = cJSON_GetObjectItemCaseSensitive(hourly, "time");
    arrays->temp = cJSON_GetObjectItemCaseSensitive(hourly, "temperature_2m");
    arrays->cloud = cJSON_GetObjectItemCaseSensitive(hourly, "cloud_cover");
    arrays->rad = cJSON_GetObjectItemCaseSensitive(hourly, "shortwave_radiation");
    if (!cJSON_IsArray(arrays->times)) {
        return -1;
    }

    return 0;
}

static int parse_archive_payload(const char *json_text, cJSON **out_root, archive_hourly_arrays *out_arrays)
{
    cJSON *root;

    if (json_text == NULL || out_root == NULL || out_arrays == NULL) {
        return -1;
    }
    *out_root = NULL;

    root = cJSON_Parse(json_text);
    if (root == NULL) {
        return -1;
    }
    if (archive_extract_arrays(root, out_arrays) != 0) {
        cJSON_Delete(root);
        return -1;
    }

    *out_root = root;
    return 0;
}

static int record_buffer_grow(record_buffer *buffer)
{
    size_t next_cap;
    ss_sdk_record *grown;

    if (buffer == NULL) {
        return -1;
    }
    next_cap = (buffer->cap == 0U) ? 256U : (buffer->cap * 2U);
    if (next_cap < buffer->cap || next_cap > SIZE_MAX / sizeof(*grown)) {
        return -1;
    }
    grown = (ss_sdk_record *)realloc(buffer->records, next_cap * sizeof(*grown));
    if (grown == NULL) {
        return -1;
    }
    buffer->records = grown;
    buffer->cap = next_cap;
    return 0;
}

static int append_backfill_record(record_buffer *buffer, ss_metric_id metric, int64_t ts_utc, double value, const payload_write_spec *spec)
{
    ss_sdk_record rec;

    if (buffer == NULL || spec == NULL) {
        return -1;
    }
    if (ss_sdk_record_make_f64(&rec, metric, value, ts_utc, spec->data_kind) != SS_SDK_OK) {
        return -1;
    }
    rec.ingested_utc = spec->ingested_utc;

    if (buffer->count == buffer->cap && record_buffer_grow(buffer) != 0) {
        return -1;
    }

    buffer->records[buffer->count] = rec;
    buffer->count += 1U;
    return 0;
}

static int payload_row_read(archive_hourly_arrays *arrays, int index, payload_metric_row *row)
{
    cJSON *jt;
    cJSON *jtemp;
    cJSON *jcloud;
    cJSON *jrad;

    if (arrays == NULL || row == NULL) {
        return -1;
    }
    jt = cJSON_GetArrayItem(arrays->times, index);
    jtemp = cJSON_GetArrayItem(arrays->temp, index);
    jcloud = cJSON_GetArrayItem(arrays->cloud, index);
    jrad = cJSON_GetArrayItem(arrays->rad, index);
    if (!cJSON_IsString(jt) || jt->valuestring == NULL) {
        return -1;
    }
    if (!cJSON_IsNumber(jtemp) || !cJSON_IsNumber(jcloud) || !cJSON_IsNumber(jrad)) {
        return -1;
    }
    if (backfill_parse_utc_hour(jt->valuestring, &row->ts_utc) != 0) {
        return -1;
    }
    row->temp = jtemp->valuedouble;
    row->cloud = jcloud->valuedouble;
    row->rad = jrad->valuedouble;
    return 0;
}

static int payload_row_is_in_window(const payload_metric_row *row, const payload_write_spec *spec)
{
    if (row == NULL || spec == NULL) {
        return 0;
    }
    if (!spec->limit_to_window) {
        return 1;
    }
    return row->ts_utc >= (spec->window_start_utc - spec->edge_padding_sec) &&
           row->ts_utc < (spec->window_end_utc + spec->edge_padding_sec);
}

static int payload_append_metric_triplet(record_buffer *buffer, const payload_metric_row *row, const payload_write_spec *spec)
{
    if (buffer == NULL || row == NULL || spec == NULL) {
        return -1;
    }
    if (append_backfill_record(buffer, SS_METRIC_WEATHER_TEMPERATURE_AIR_2M_C, row->ts_utc, row->temp, spec) != 0 ||
        append_backfill_record(buffer, SS_METRIC_WEATHER_CLOUD_COVER_TOTAL_PCT, row->ts_utc, row->cloud, spec) != 0 ||
        append_backfill_record(buffer, SS_METRIC_WEATHER_RADIATION_SHORTWAVE_WM2, row->ts_utc, row->rad, spec) != 0) {
        return -1;
    }
    return 0;
}

static int append_payload_records(archive_hourly_arrays *arrays, const payload_write_spec *spec, record_buffer *buffer)
{
    int len;
    int i;

    if (arrays == NULL || spec == NULL || buffer == NULL) {
        return -1;
    }

    len = cJSON_GetArraySize(arrays->times);
    for (i = 0; i < len; ++i) {
        payload_metric_row row;

        if (payload_row_read(arrays, i, &row) != 0) {
            continue;
        }
        if (!payload_row_is_in_window(&row, spec)) {
            continue;
        }
        if (payload_append_metric_triplet(buffer, &row, spec) != 0) {
            return -1;
        }
    }

    return 0;
}

static int write_payload_records(const char *json_text, const payload_write_spec *spec, size_t *out_writes)
{
    cJSON *root = NULL;
    archive_hourly_arrays arrays;
    record_buffer buffer;
    size_t written = 0U;
    int rc = -1;

    if (json_text == NULL || spec == NULL) {
        return -1;
    }

    memset(&buffer, 0, sizeof(buffer));
    if (parse_archive_payload(json_text, &root, &arrays) != 0) {
        return -1;
    }
    if (append_payload_records(&arrays, spec, &buffer) != 0) {
        goto cleanup;
    }

    if (buffer.count > 0U && ss_sdk_db_write_records(buffer.records, buffer.count, &written) != SS_SDK_OK) {
        goto cleanup;
    }

    if (out_writes != NULL) {
        *out_writes = written;
    }
    rc = 0;

cleanup:
    free(buffer.records);
    cJSON_Delete(root);
    return rc;
}

int backfill_write_archive_payload(const char *json_text, int64_t chunk_start_utc, int64_t chunk_end_utc, size_t *out_writes)
{
    payload_write_spec spec;

    if (json_text == NULL || chunk_start_utc >= chunk_end_utc) {
        return -1;
    }

    memset(&spec, 0, sizeof(spec));
    spec.limit_to_window = 1;
    spec.window_start_utc = chunk_start_utc;
    spec.window_end_utc = chunk_end_utc;
    spec.edge_padding_sec = 3600;
    spec.data_kind = SS_SDK_DATA_OBSERVATION;
    spec.ingested_utc = 0;
    return write_payload_records(json_text, &spec, out_writes);
}

int backfill_write_single_run_payload(const char *json_text, int64_t run_utc, size_t *out_writes)
{
    payload_write_spec spec;

    if (json_text == NULL || run_utc < 0) {
        return -1;
    }

    memset(&spec, 0, sizeof(spec));
    spec.limit_to_window = 0;
    spec.data_kind = SS_SDK_DATA_FORECAST;
    spec.ingested_utc = run_utc;
    return write_payload_records(json_text, &spec, out_writes);
}

int backfill_build_archive_url(char out_url[BACKFILL_MAX_URL_LEN], const backfill_config *cfg, int64_t from_utc, int64_t to_utc)
{
    char start_date[11];
    char end_date[11];
    int64_t inclusive_end = to_utc > 0 ? to_utc - 1 : to_utc;
    const char *joiner;
    int n;

    if (out_url == NULL || cfg == NULL || from_utc >= to_utc) {
        return -1;
    }
    if (backfill_epoch_to_ymd_utc(from_utc, start_date) != 0) {
        return -1;
    }
    if (backfill_epoch_to_ymd_utc(inclusive_end, end_date) != 0) {
        return -1;
    }

    joiner = strchr(cfg->endpoint, '?') != NULL ? "&" : "?";
    n = snprintf(
        out_url,
        BACKFILL_MAX_URL_LEN,
        "%s%slatitude=%.6f&longitude=%.6f&start_date=%s&end_date=%s&hourly=temperature_2m,cloud_cover,shortwave_radiation&timezone=UTC",
        cfg->endpoint,
        joiner,
        cfg->latitude,
        cfg->longitude,
        start_date,
        end_date);
    if (n <= 0 || n >= BACKFILL_MAX_URL_LEN) {
        return -1;
    }
    return 0;
}

int backfill_build_single_run_url(char out_url[BACKFILL_MAX_URL_LEN], const backfill_config *cfg, int64_t run_utc)
{
    char run_str[17];
    int n;

    if (out_url == NULL || cfg == NULL || run_utc < 0) {
        return -1;
    }
    if (backfill_epoch_to_ymdhm_utc(run_utc, run_str) != 0) {
        return -1;
    }

    n = snprintf(
        out_url,
        BACKFILL_MAX_URL_LEN,
        "%s?latitude=%.6f&longitude=%.6f&hourly=temperature_2m,cloud_cover,shortwave_radiation&run=%s&forecast_days=%d&timezone=UTC",
        cfg->single_runs_endpoint,
        cfg->latitude,
        cfg->longitude,
        run_str,
        BACKFILL_FORECAST_DAYS_PER_RUN);
    if (n <= 0 || n >= BACKFILL_MAX_URL_LEN) {
        return -1;
    }
    return 0;
}
