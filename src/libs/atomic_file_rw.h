#ifndef ATOMIC_FILE_RW_H
#define ATOMIC_FILE_RW_H

#include <errno.h>
#include <stddef.h>

/*
 * Compatibility shim: legacy atomic_file_rw API now forwards to SDK storage.
 *
 * Call sites in fetch/compute remain unchanged (`af_save`/`af_read`), but the
 * persistence backend is the SDK canonical DB API.
 */

// Default legacy path constant kept for source compatibility.
#define ATOMIC_FILE_DEFAULT_PATH ".db/database.jsonl"

// --- Standardized Constants ---

// Sources
#define AF_SOURCE_API_SMHI "API_smhi"
#define AF_SOURCE_SENSOR "Sensor_Network"
#define AF_SOURCE_CALCULATOR "Calculator_Svc"
#define AF_SOURCE_SYSTEM "System_Log"

// Types
#define AF_TYPE_TEMPERATURE "temperature"
#define AF_TYPE_HUMIDITY "humidity"
#define AF_TYPE_POWER "power_usage"
#define AF_TYPE_LOG "log_entry"
#define AF_TYPE_ERROR "error_report"

int af_save(const char* source, const char* type, const char* data);
char* af_read(size_t* out_size);

#endif  // ATOMIC_FILE_RW_H

#if defined(ATOMIC_FILE_RW_IMPLEMENTATION)

#include "json/cJSON.h"
#include "../sdk/ss_sdk.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static int64_t af_now_slot_utc(void)
{
    const time_t now = time(NULL);
    const int64_t now_utc = (int64_t)now;

    if (now_utc < 0) {
        return 0;
    }

    return now_utc - (now_utc % 900);
}

static int64_t af_align_slot_utc(int64_t ts_utc)
{
    if (ts_utc < 0) {
        return 0;
    }
    return ts_utc - (ts_utc % 900);
}

static int af_parse_iso8601_hour_local(const char *s, int64_t *out_ts_utc)
{
    struct tm tmv;
    int year;
    int mon;
    int day;
    int hour;
    int min;
    time_t parsed;

    if (s == NULL || out_ts_utc == NULL) {
        return -1;
    }

    if (strlen(s) < 16U) {
        return -1;
    }

    if (sscanf(s, "%4d-%2d-%2dT%2d:%2d", &year, &mon, &day, &hour, &min) != 5) {
        return -1;
    }

    memset(&tmv, 0, sizeof(tmv));
    tmv.tm_year = year - 1900;
    tmv.tm_mon = mon - 1;
    tmv.tm_mday = day;
    tmv.tm_hour = hour;
    tmv.tm_min = min;
    tmv.tm_isdst = -1;

    /* Legacy fetch payloads carry local-offset timestamps; shim treats them
     * as local wall-clock to preserve old non-UTC behavior tolerance. */
    parsed = mktime(&tmv);
    if (parsed == (time_t)-1) {
        return -1;
    }

    *out_ts_utc = (int64_t)parsed;
    return 0;
}

static int af_sdk_status_to_errno(ss_sdk_status st)
{
    switch (st) {
        case SS_SDK_OK:
            return 0;
        case SS_SDK_ERR_INVALID_ARG:
            return EINVAL;
        case SS_SDK_ERR_VALIDATION:
            return EINVAL;
        case SS_SDK_ERR_PARTIAL_DATA:
            return ENODATA;
        default:
            return EIO;
    }
}

static int af_write_metric_f64(ss_metric_id metric, double value, int64_t ts_start_utc, ss_sdk_data_kind data_kind)
{
    ss_sdk_record rec;
    ss_sdk_status st;

    memset(&rec, 0, sizeof(rec));
    rec.metric = metric;
    rec.value_type = SS_SDK_VALUE_F64;
    rec.value.f64 = value;
    rec.ts_start_utc = af_align_slot_utc(ts_start_utc);
    rec.ts_end_utc = rec.ts_start_utc + 900;
    rec.data_kind = data_kind;

    st = ss_sdk_db_write_record(&rec);
    if (st != SS_SDK_OK) {
        errno = af_sdk_status_to_errno(st);
        return -1;
    }

    return 0;
}

static int af_write_metric_i64(ss_metric_id metric, int64_t value, int64_t ts_start_utc, ss_sdk_data_kind data_kind)
{
    ss_sdk_record rec;
    ss_sdk_status st;

    memset(&rec, 0, sizeof(rec));
    rec.metric = metric;
    rec.value_type = SS_SDK_VALUE_I64;
    rec.value.i64 = value;
    rec.ts_start_utc = af_align_slot_utc(ts_start_utc);
    rec.ts_end_utc = rec.ts_start_utc + 900;
    rec.data_kind = data_kind;

    st = ss_sdk_db_write_record(&rec);
    if (st != SS_SDK_OK) {
        errno = af_sdk_status_to_errno(st);
        return -1;
    }

    return 0;
}

static int af_write_openmeteo_payload(const cJSON *root)
{
    const cJSON *current_obj;
    const cJSON *hourly_obj;
    const cJSON *time_obj;
    int64_t base_slot = af_now_slot_utc();

    if (root == NULL || !cJSON_IsObject(root)) {
        return 0;
    }

    current_obj = cJSON_GetObjectItemCaseSensitive(root, "current");
    if (current_obj != NULL && cJSON_IsObject(current_obj)) {
        const cJSON *temperature_obj = cJSON_GetObjectItemCaseSensitive(current_obj, "temperature_2m");
        const cJSON *wind_speed_obj = cJSON_GetObjectItemCaseSensitive(current_obj, "wind_speed_10m");
        const cJSON *humidity_obj = cJSON_GetObjectItemCaseSensitive(current_obj, "relative_humidity_2m");
        const cJSON *weather_code_obj = cJSON_GetObjectItemCaseSensitive(current_obj, "weather_code");

        time_obj = cJSON_GetObjectItemCaseSensitive(current_obj, "time");
        if (time_obj != NULL && cJSON_IsNumber(time_obj)) {
            base_slot = af_align_slot_utc((int64_t)time_obj->valuedouble);
        } else if (time_obj != NULL && cJSON_IsString(time_obj) && time_obj->valuestring != NULL) {
            int64_t parsed_ts;
            if (af_parse_iso8601_hour_local(time_obj->valuestring, &parsed_ts) == 0) {
                base_slot = af_align_slot_utc(parsed_ts);
            }
        }

        if (temperature_obj != NULL && cJSON_IsNumber(temperature_obj)) {
            if (af_write_metric_f64(
                    SS_METRIC_WEATHER_TEMPERATURE_AIR_2M_C,
                    temperature_obj->valuedouble,
                    base_slot,
                    SS_SDK_DATA_OBSERVATION) != 0) {
                return -1;
            }
        }

        if (wind_speed_obj != NULL && cJSON_IsNumber(wind_speed_obj)) {
            if (af_write_metric_f64(
                    SS_METRIC_WEATHER_WIND_SPEED_10M_MS,
                    wind_speed_obj->valuedouble,
                    base_slot,
                    SS_SDK_DATA_OBSERVATION) != 0) {
                return -1;
            }
        }

        if (humidity_obj != NULL && cJSON_IsNumber(humidity_obj)) {
            if (af_write_metric_f64(
                    SS_METRIC_WEATHER_HUMIDITY_RELATIVE_2M_PCT,
                    humidity_obj->valuedouble,
                    base_slot,
                    SS_SDK_DATA_OBSERVATION) != 0) {
                return -1;
            }
        }

        if (weather_code_obj != NULL && cJSON_IsNumber(weather_code_obj)) {
            if (af_write_metric_i64(
                    SS_METRIC_WEATHER_CONDITION_SYMBOL_CODE,
                    (int64_t)weather_code_obj->valuedouble,
                    base_slot,
                    SS_SDK_DATA_OBSERVATION) != 0) {
                return -1;
            }
        }
    }

    hourly_obj = cJSON_GetObjectItemCaseSensitive(root, "hourly");
    if (hourly_obj != NULL && cJSON_IsObject(hourly_obj)) {
        const cJSON *hourly_time = cJSON_GetObjectItemCaseSensitive(hourly_obj, "time");
        const cJSON *hourly_temp = cJSON_GetObjectItemCaseSensitive(hourly_obj, "temperature_2m");
        const cJSON *hourly_wind = cJSON_GetObjectItemCaseSensitive(hourly_obj, "wind_speed_10m");
        const cJSON *hourly_humidity = cJSON_GetObjectItemCaseSensitive(hourly_obj, "relative_humidity_2m");
        int n;
        int i;

        if (hourly_time != NULL && cJSON_IsArray(hourly_time)) {
            n = cJSON_GetArraySize(hourly_time);
            for (i = 0; i < n; ++i) {
                int64_t slot_utc = base_slot + (int64_t)i * 3600;
                const cJSON *t = cJSON_GetArrayItem(hourly_time, i);
                const cJSON *vtemp = (hourly_temp != NULL && cJSON_IsArray(hourly_temp)) ? cJSON_GetArrayItem(hourly_temp, i) : NULL;
                const cJSON *vwind = (hourly_wind != NULL && cJSON_IsArray(hourly_wind)) ? cJSON_GetArrayItem(hourly_wind, i) : NULL;
                const cJSON *vhum = (hourly_humidity != NULL && cJSON_IsArray(hourly_humidity)) ? cJSON_GetArrayItem(hourly_humidity, i) : NULL;

                if (t != NULL && cJSON_IsNumber(t)) {
                    slot_utc = af_align_slot_utc((int64_t)t->valuedouble);
                } else if (t != NULL && cJSON_IsString(t) && t->valuestring != NULL) {
                    int64_t parsed_ts;
                    if (af_parse_iso8601_hour_local(t->valuestring, &parsed_ts) == 0) {
                        slot_utc = af_align_slot_utc(parsed_ts);
                    }
                }

                if (vtemp != NULL && cJSON_IsNumber(vtemp)) {
                    if (af_write_metric_f64(
                            SS_METRIC_WEATHER_TEMPERATURE_AIR_2M_C,
                            vtemp->valuedouble,
                            slot_utc,
                            SS_SDK_DATA_FORECAST) != 0) {
                        return -1;
                    }
                }

                if (vwind != NULL && cJSON_IsNumber(vwind)) {
                    if (af_write_metric_f64(
                            SS_METRIC_WEATHER_WIND_SPEED_10M_MS,
                            vwind->valuedouble,
                            slot_utc,
                            SS_SDK_DATA_FORECAST) != 0) {
                        return -1;
                    }
                }

                if (vhum != NULL && cJSON_IsNumber(vhum)) {
                    if (af_write_metric_f64(
                            SS_METRIC_WEATHER_HUMIDITY_RELATIVE_2M_PCT,
                            vhum->valuedouble,
                            slot_utc,
                            SS_SDK_DATA_FORECAST) != 0) {
                        return -1;
                    }
                }
            }
        }
    }

    return 0;
}

static int af_write_elpris_payload(const cJSON *root)
{
    int n;
    int i;

    if (root == NULL || !cJSON_IsArray(root)) {
        return 0;
    }

    n = cJSON_GetArraySize(root);
    for (i = 0; i < n; ++i) {
        const cJSON *row = cJSON_GetArrayItem(root, i);
        const cJSON *sek_obj;
        const cJSON *eur_obj;
        const cJSON *time_start_obj;
        int64_t slot_utc = af_now_slot_utc() + (int64_t)i * 3600;

        if (row == NULL || !cJSON_IsObject(row)) {
            continue;
        }

        time_start_obj = cJSON_GetObjectItemCaseSensitive(row, "time_start");
        if (time_start_obj != NULL && cJSON_IsString(time_start_obj) && time_start_obj->valuestring != NULL) {
            int64_t parsed_ts;
            if (af_parse_iso8601_hour_local(time_start_obj->valuestring, &parsed_ts) == 0) {
                slot_utc = af_align_slot_utc(parsed_ts);
            }
        }

        sek_obj = cJSON_GetObjectItemCaseSensitive(row, "SEK_per_kWh");
        if (sek_obj != NULL && cJSON_IsNumber(sek_obj)) {
            if (af_write_metric_f64(
                    SS_METRIC_ENERGY_PRICE_SPOT_SEK_KWH,
                    sek_obj->valuedouble,
                    slot_utc,
                    SS_SDK_DATA_FORECAST) != 0) {
                return -1;
            }
        }

        eur_obj = cJSON_GetObjectItemCaseSensitive(row, "EUR_per_kWh");
        if (eur_obj != NULL && cJSON_IsNumber(eur_obj)) {
            if (af_write_metric_f64(
                    SS_METRIC_ENERGY_PRICE_SPOT_EUR_KWH,
                    eur_obj->valuedouble,
                    slot_utc,
                    SS_SDK_DATA_FORECAST) != 0) {
                return -1;
            }
        }
    }

    return 0;
}

static int af_str_ieq(const char *a, const char *b)
{
    size_t i;

    if (a == NULL || b == NULL) {
        return 0;
    }

    for (i = 0; a[i] != '\0' && b[i] != '\0'; ++i) {
        char ca = a[i];
        char cb = b[i];

        if (ca >= 'A' && ca <= 'Z') {
            ca = (char)(ca - 'A' + 'a');
        }
        if (cb >= 'A' && cb <= 'Z') {
            cb = (char)(cb - 'A' + 'a');
        }

        if (ca != cb) {
            return 0;
        }
    }

    return a[i] == '\0' && b[i] == '\0';
}

int af_save(const char* source, const char* type, const char* data)
{
    cJSON *root;
    int rc = 0;

    if (source == NULL || type == NULL || data == NULL) {
        errno = EINVAL;
        return -1;
    }

    (void)type;

    root = cJSON_Parse(data);
    if (root == NULL) {
        SS_LOG_WARN("atomic.sdk.parse_failed", "atomic wrapper payload parse failed; event dropped");
        return 0;
    }

    if (af_str_ieq(source, "Openmeteo") || af_str_ieq(source, "openmeteo")) {
        rc = af_write_openmeteo_payload(root);
    } else if (af_str_ieq(source, "Elprisjustnu") || af_str_ieq(source, "elprisjustnu")) {
        rc = af_write_elpris_payload(root);
    } else {
        /* Unknown legacy source: accept call for compatibility, write log only. */
        SS_LOG_INFO("atomic.sdk.unknown_source", "atomic wrapper received unknown source payload");
        rc = 0;
    }

    cJSON_Delete(root);
    return rc;
}

static int af_append_text(char **buf, size_t *len, size_t *cap, const char *text)
{
    size_t n;
    char *grown;

    if (buf == NULL || len == NULL || cap == NULL || text == NULL) {
        errno = EINVAL;
        return -1;
    }

    n = strlen(text);
    if (*len + n + 1U > *cap) {
        size_t next = *cap;
        while (*len + n + 1U > next) {
            next *= 2U;
        }
        grown = (char *)realloc(*buf, next);
        if (grown == NULL) {
            errno = ENOMEM;
            return -1;
        }
        *buf = grown;
        *cap = next;
    }

    memcpy(*buf + *len, text, n);
    *len += n;
    (*buf)[*len] = '\0';
    return 0;
}

char* af_read(size_t* out_size)
{
    static const ss_metric_id k_metrics[] = {
        SS_METRIC_WEATHER_TEMPERATURE_AIR_2M_C,
        SS_METRIC_WEATHER_HUMIDITY_RELATIVE_2M_PCT,
        SS_METRIC_WEATHER_WIND_SPEED_10M_MS,
        SS_METRIC_ENERGY_PRICE_SPOT_SEK_KWH,
        SS_METRIC_ENERGY_PRICE_SPOT_EUR_KWH,
        SS_METRIC_WEATHER_CONDITION_SYMBOL_CODE
    };

    char *buf;
    size_t len = 0;
    size_t cap = 1024;
    size_t i;

    buf = (char *)malloc(cap);
    if (buf == NULL) {
        errno = ENOMEM;
        return NULL;
    }
    buf[0] = '\0';

    for (i = 0; i < sizeof(k_metrics) / sizeof(k_metrics[0]); ++i) {
        ss_sdk_samples_out out = {NULL, 0};
        ss_sdk_status st;

        st = ss_sdk_db_get_canonical(0, 1, k_metrics[i], &out);
        if (st == SS_SDK_ERR_INTERNAL || st == SS_SDK_ERR_INVALID_ARG || st == SS_SDK_ERR_VALIDATION) {
            free(buf);
            errno = af_sdk_status_to_errno(st);
            return NULL;
        }

        if (out.count > 0) {
            const ss_sdk_sample *s = &out.samples[0];
            const char *name = ss_metric_name(s->canonical);
            char line[512];

            /* Serialize SDK samples to legacy JSONL lines for existing consumers. */
            if (s->value_type == SS_SDK_VALUE_F64) {
                snprintf(
                    line,
                    sizeof(line),
                    "{\"ts_start_utc\":%lld,\"canonical\":\"%s\",\"value_type\":\"f64\",\"value\":%.10g,\"flags\":%u}\n",
                    (long long)s->ts_utc,
                    (name == NULL) ? "" : name,
                    s->value.f64,
                    (unsigned int)s->flags);
            } else if (s->value_type == SS_SDK_VALUE_I64) {
                snprintf(
                    line,
                    sizeof(line),
                    "{\"ts_start_utc\":%lld,\"canonical\":\"%s\",\"value_type\":\"i64\",\"value\":%lld,\"flags\":%u}\n",
                    (long long)s->ts_utc,
                    (name == NULL) ? "" : name,
                    (long long)s->value.i64,
                    (unsigned int)s->flags);
            } else if (s->value_type == SS_SDK_VALUE_BOOL) {
                snprintf(
                    line,
                    sizeof(line),
                    "{\"ts_start_utc\":%lld,\"canonical\":\"%s\",\"value_type\":\"bool\",\"value\":%s,\"flags\":%u}\n",
                    (long long)s->ts_utc,
                    (name == NULL) ? "" : name,
                    s->value.boolean ? "true" : "false",
                    (unsigned int)s->flags);
            } else {
                line[0] = '\0';
            }

            if (line[0] != '\0') {
                if (af_append_text(&buf, &len, &cap, line) != 0) {
                    ss_sdk_db_free_samples(&out);
                    free(buf);
                    return NULL;
                }
            }
        }

        ss_sdk_db_free_samples(&out);
    }

    if (out_size != NULL) {
        *out_size = len;
    }

    return buf;
}

#endif  // ATOMIC_FILE_RW_IMPLEMENTATION
