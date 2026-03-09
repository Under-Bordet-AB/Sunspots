#define _GNU_SOURCE
#include "backfill_price_payload.h"

#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "../sdk/ss_sdk.h"

typedef struct {
    ss_sdk_record *records;
    size_t count;
    size_t cap;
} price_record_buffer;

static int parse_fixed_digits(const char *text, size_t offset, size_t width, int *out_value)
{
    char buf[8];
    char *endp = NULL;
    long parsed;

    if (text == NULL || out_value == NULL || width == 0U || width >= sizeof(buf)) {
        return -1;
    }
    memcpy(buf, text + offset, width);
    buf[width] = '\0';
    parsed = strtol(buf, &endp, 10);
    if (endp == NULL || *endp != '\0' || parsed < 0L || parsed > INT_MAX) {
        return -1;
    }
    *out_value = (int)parsed;
    return 0;
}

static int price_record_buffer_grow(price_record_buffer *buffer)
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

static int parse_price_time_start(const char *time_start, int64_t *out_ts_utc)
{
    struct tm tmv;
    int year;
    int month;
    int day;
    int hour;
    int minute;
    int second;
    char sign;
    int tz_hour;
    int tz_minute;
    int tz_offset_seconds;
    time_t parsed_utc;

    if (time_start == NULL || out_ts_utc == NULL) {
        return -1;
    }
    size_t len = strlen(time_start);

    if ((len != 24U && len != 25U) ||
        time_start[4] != '-' ||
        time_start[7] != '-' ||
        time_start[10] != 'T' ||
        time_start[13] != ':' ||
        time_start[16] != ':' ||
        (time_start[19] != '+' && time_start[19] != '-')) {
        return -1;
    }
    if (parse_fixed_digits(time_start, 0U, 4U, &year) != 0 ||
        parse_fixed_digits(time_start, 5U, 2U, &month) != 0 ||
        parse_fixed_digits(time_start, 8U, 2U, &day) != 0 ||
        parse_fixed_digits(time_start, 11U, 2U, &hour) != 0 ||
        parse_fixed_digits(time_start, 14U, 2U, &minute) != 0 ||
        parse_fixed_digits(time_start, 17U, 2U, &second) != 0) {
        return -1;
    }
    sign = time_start[19];
    if (len == 25U) {
        if (time_start[22] != ':' ||
            parse_fixed_digits(time_start, 20U, 2U, &tz_hour) != 0 ||
            parse_fixed_digits(time_start, 23U, 2U, &tz_minute) != 0) {
            return -1;
        }
    } else {
        if (parse_fixed_digits(time_start, 20U, 2U, &tz_hour) != 0 ||
            parse_fixed_digits(time_start, 22U, 2U, &tz_minute) != 0) {
            return -1;
        }
    }

    memset(&tmv, 0, sizeof(tmv));
    tmv.tm_year = year - 1900;
    tmv.tm_mon = month - 1;
    tmv.tm_mday = day;
    tmv.tm_hour = hour;
    tmv.tm_min = minute;
    tmv.tm_sec = second;
    tmv.tm_isdst = 0;
    parsed_utc = timegm(&tmv);
    if (parsed_utc == (time_t)-1) {
        return -1;
    }
    tz_offset_seconds = (tz_hour * 3600) + (tz_minute * 60);
    if (sign == '+') {
        *out_ts_utc = (int64_t)parsed_utc - (int64_t)tz_offset_seconds;
    } else if (sign == '-') {
        *out_ts_utc = (int64_t)parsed_utc + (int64_t)tz_offset_seconds;
    } else {
        return -1;
    }
    return 0;
}

static int append_price_record(
    price_record_buffer *buffer,
    ss_metric_id metric,
    double value,
    int64_t ts_utc)
{
    ss_sdk_record rec;

    if (buffer == NULL) {
        return -1;
    }
    if (ss_sdk_record_make_f64(&rec, metric, value, ts_utc, SS_SDK_DATA_OBSERVATION) != SS_SDK_OK) {
        return -1;
    }
    if (buffer->count == buffer->cap && price_record_buffer_grow(buffer) != 0) {
        return -1;
    }
    buffer->records[buffer->count] = rec;
    buffer->count += 1U;
    return 0;
}

static int append_price_rows(const cJSON *root, price_record_buffer *buffer)
{
    int count;
    int i;

    if (!cJSON_IsArray(root) || buffer == NULL) {
        return -1;
    }

    count = cJSON_GetArraySize((cJSON *)root);
    for (i = 0; i < count; ++i) {
        cJSON *row = cJSON_GetArrayItem((cJSON *)root, i);
        cJSON *time_start;
        cJSON *sek;
        cJSON *eur;
        int64_t ts_utc;

        if (!cJSON_IsObject(row)) {
            continue;
        }
        time_start = cJSON_GetObjectItemCaseSensitive(row, "time_start");
        if (!cJSON_IsString(time_start) || time_start->valuestring == NULL) {
            continue;
        }
        if (parse_price_time_start(time_start->valuestring, &ts_utc) != 0) {
            continue;
        }

        sek = cJSON_GetObjectItemCaseSensitive(row, "SEK_per_kWh");
        if (cJSON_IsNumber(sek) &&
            append_price_record(buffer, SS_METRIC_ENERGY_PRICE_SPOT_SEK_KWH, sek->valuedouble, ts_utc) != 0) {
            return -1;
        }

        eur = cJSON_GetObjectItemCaseSensitive(row, "EUR_per_kWh");
        if (cJSON_IsNumber(eur) &&
            append_price_record(buffer, SS_METRIC_ENERGY_PRICE_SPOT_EUR_KWH, eur->valuedouble, ts_utc) != 0) {
            return -1;
        }
    }

    return 0;
}

int backfill_write_elprisjustnu_payload(const char *json_text, int64_t delivery_day_utc, size_t *out_writes)
{
    cJSON *root = NULL;
    price_record_buffer buffer;
    size_t written = 0U;
    int rc = -1;

    if (json_text == NULL || delivery_day_utc < 0) {
        return -1;
    }

    memset(&buffer, 0, sizeof(buffer));
    root = cJSON_Parse(json_text);
    if (root == NULL) {
        goto cleanup;
    }
    (void)delivery_day_utc;
    if (append_price_rows(root, &buffer) != 0) {
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

int backfill_build_elprisjustnu_day_url(char out_url[BACKFILL_MAX_URL_LEN], const backfill_config *cfg, int year, int month, int day)
{
    int n;

    if (out_url == NULL || cfg == NULL || cfg->elprisomrade[0] == '\0') {
        return -1;
    }

    n = snprintf(
        out_url,
        BACKFILL_MAX_URL_LEN,
        "%s/%04d/%02d-%02d_%s.json",
        cfg->endpoint,
        year,
        month,
        day,
        cfg->elprisomrade);
    if (n <= 0 || n >= BACKFILL_MAX_URL_LEN) {
        return -1;
    }
    return 0;
}
