#include "sdk/internal/db/ss_db_internal.h"

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <limits.h>
#include <locale.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#define SS_SDK_DB_DEFAULT_PATH "logs/ss_sdk_db.tsv"
#define SS_DB_FIELD_COUNT 12

typedef struct {
    size_t count;
    /* Flexible array result block so caller receives one pointer to free. */
    ss_sdk_record records[];
} ss_db_result_block;

static char *ss_strdup_local(const char *s)
{
    size_t n;
    char *p;

    if (s == NULL) {
        return NULL;
    }
    n = strlen(s) + 1;
    p = (char *)malloc(n);
    if (p == NULL) {
        return NULL;
    }
    memcpy(p, s, n);
    return p;
}

static const char *ss_db_path(void)
{
    const char *override = getenv("SS_SDK_DB_PATH");
    if (override != NULL && override[0] != '\0') {
        return override;
    }
    return SS_SDK_DB_DEFAULT_PATH;
}

static int ss_checked_add(size_t *current_size, size_t add)
{
    /* BUGFIX(#34): explicit overflow guard in escape/serialization sizing. */
    if (*current_size > SIZE_MAX - add) {
        errno = EOVERFLOW;
        return -1;
    }
    *current_size += add;
    return 0;
}

static int ss_ensure_parent_dirs(const char *path)
{
    char *tmp;
    char *p;

    tmp = ss_strdup_local(path);
    if (tmp == NULL) {
        return -1;
    }

    p = tmp;
    while (*p != '\0') {
        if (*p == '/') {
            *p = '\0';
            if (tmp[0] != '\0' && mkdir(tmp, 0775) != 0 && errno != EEXIST) {
                free(tmp);
                return -1;
            }
            *p = '/';
        }
        ++p;
    }

    free(tmp);
    return 0;
}

static char *ss_read_all_fd(int fd, size_t *out_size)
{
    struct stat st;
    char *buf;
    ssize_t nread;
    size_t total;

    if (fstat(fd, &st) != 0) {
        return NULL;
    }

    if (st.st_size < 0) {
        return NULL;
    }

    buf = (char *)malloc((size_t)st.st_size + 1U);
    if (buf == NULL) {
        return NULL;
    }

    if (lseek(fd, 0, SEEK_SET) < 0) {
        free(buf);
        return NULL;
    }

    total = 0;
    while (total < (size_t)st.st_size) {
        nread = read(fd, buf + total, (size_t)st.st_size - total);
        if (nread < 0) {
            if (errno == EINTR) {
                continue;
            }
            free(buf);
            return NULL;
        }
        if (nread == 0) {
            break;
        }
        total += (size_t)nread;
    }

    buf[total] = '\0';
    if (out_size != NULL) {
        *out_size = total;
    }
    return buf;
}

static int ss_write_all(int fd, const char *buf, size_t len)
{
    ssize_t nw;
    size_t off = 0;

    while (off < len) {
        nw = write(fd, buf + off, len - off);
        if (nw < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        /* BUGFIX(#24): zero-byte write is treated as hard I/O failure. */
        if (nw == 0) {
            errno = EIO;
            return -1;
        }
        off += (size_t)nw;
    }

    return 0;
}

static char *ss_escape(const char *s)
{
    size_t n = 0;
    size_t i;
    char *out;
    size_t j = 0;

    if (s == NULL) {
        return ss_strdup_local("");
    }

    /* On-disk format is tab-separated; escape separators/control chars. */
    for (i = 0; s[i] != '\0'; ++i) {
        if (s[i] == '\\' || s[i] == '\t' || s[i] == '\n') {
            if (ss_checked_add(&n, 2U) != 0) {
                return NULL;
            }
        } else if (ss_checked_add(&n, 1U) != 0) {
            return NULL;
        }
    }

    if (ss_checked_add(&n, 1U) != 0) {
        return NULL;
    }

    out = (char *)malloc(n);
    if (out == NULL) {
        return NULL;
    }

    for (i = 0; s[i] != '\0'; ++i) {
        if (s[i] == '\\') {
            out[j++] = '\\';
            out[j++] = '\\';
        } else if (s[i] == '\t') {
            out[j++] = '\\';
            out[j++] = 't';
        } else if (s[i] == '\n') {
            out[j++] = '\\';
            out[j++] = 'n';
        } else {
            out[j++] = s[i];
        }
    }
    out[j] = '\0';
    return out;
}

static void ss_unescape_inplace(char *s)
{
    char *src = s;
    char *dst = s;

    while (*src != '\0') {
        if (*src == '\\') {
            ++src;
            if (*src == 't') {
                *dst++ = '\t';
                ++src;
            } else if (*src == 'n') {
                *dst++ = '\n';
                ++src;
            } else if (*src == '\\') {
                *dst++ = '\\';
                ++src;
            } else if (*src == '\0') {
                break;
            } else {
                *dst++ = *src++;
            }
        } else {
            *dst++ = *src++;
        }
    }

    *dst = '\0';
}

static void ss_record_reset(ss_sdk_record *rec)
{
    memset(rec, 0, sizeof(*rec));
}

static void ss_record_free_strings(ss_sdk_record *rec)
{
    if (rec == NULL) {
        return;
    }

    free((char *)rec->source_api);
    free((char *)rec->source_field);
    free((char *)rec->source_tz);
    free((char *)rec->model_id);

    if (rec->value_type == SS_SDK_VALUE_STR) {
        free((char *)rec->value.str);
    }

    rec->source_api = NULL;
    rec->source_field = NULL;
    rec->source_tz = NULL;
    rec->model_id = NULL;
    if (rec->value_type == SS_SDK_VALUE_STR) {
        rec->value.str = NULL;
    }
}

static int ss_split_fields(char *line, char *fields[SS_DB_FIELD_COUNT])
{
    int i = 0;
    char *p = line;

    while (i < SS_DB_FIELD_COUNT) {
        fields[i++] = p;
        p = strchr(p, '\t');
        if (p == NULL) {
            break;
        }
        *p = '\0';
        ++p;
    }

    return i;
}

static int ss_parse_i64(const char *s, int64_t *out)
{
    char *end;
    long long v;

    errno = 0;
    v = strtoll(s, &end, 10);
    if (errno != 0 || end == s || *end != '\0') {
        return -1;
    }
    *out = (int64_t)v;
    return 0;
}

static int ss_parse_int(const char *s, int *out)
{
    char *end;
    long v;

    errno = 0;
    v = strtol(s, &end, 10);
    if (errno != 0 || end == s || *end != '\0' || v < INT_MIN || v > INT_MAX) {
        return -1;
    }
    *out = (int)v;
    return 0;
}

static int ss_parse_double(const char *s, double *out)
{
    locale_t old_loc;
    locale_t c_loc;
    char *end;
    double v;
    int saved_errno;

    /* BUGFIX(#38): parse legacy decimal floats in a forced C locale. */
    c_loc = newlocale(LC_NUMERIC_MASK, "C", (locale_t)0);
    if (c_loc == (locale_t)0) {
        return -1;
    }
    old_loc = uselocale(c_loc);

    errno = 0;
    v = strtod(s, &end);
    saved_errno = errno;

    (void)uselocale(old_loc);
    freelocale(c_loc);

    if (saved_errno != 0 || end == s || *end != '\0') {
        return -1;
    }
    *out = v;
    return 0;
}

static bool ss_is_valid_metric(ss_metric_id metric)
{
    return ss_metric_meta_get(metric) != NULL;
}

static bool ss_is_valid_value_type(ss_sdk_value_type value_type)
{
    return value_type == SS_SDK_VALUE_I64 ||
           value_type == SS_SDK_VALUE_F64 ||
           value_type == SS_SDK_VALUE_STR ||
           value_type == SS_SDK_VALUE_BOOL;
}

static bool ss_is_valid_data_kind(ss_sdk_data_kind data_kind)
{
    return data_kind == SS_SDK_DATA_OBSERVATION ||
           data_kind == SS_SDK_DATA_FORECAST;
}

static bool ss_is_nonempty(const char *s)
{
    return s != NULL && s[0] != '\0';
}

static int ss_validate_parsed_record(const ss_sdk_record *rec)
{
    const ss_metric_meta *meta;

    if (!ss_is_valid_metric(rec->metric) ||
        !ss_is_valid_value_type(rec->value_type) ||
        !ss_is_valid_data_kind(rec->data_kind)) {
        return -1;
    }

    meta = ss_metric_meta_get(rec->metric);
    if (meta == NULL || meta->value_type != rec->value_type) {
        return -1;
    }

    if (rec->ts_start_utc <= 0 || rec->ts_end_utc <= 0 || rec->ts_end_utc < rec->ts_start_utc) {
        return -1;
    }

    if (!ss_is_nonempty(rec->source_api) || !ss_is_nonempty(rec->source_field)) {
        return -1;
    }

    if (rec->data_kind == SS_SDK_DATA_FORECAST) {
        if (!ss_is_nonempty(rec->model_id)) {
            return -1;
        }
        if (rec->model_run_utc <= 0 || rec->issued_at_utc <= 0) {
            return -1;
        }
    }

    if (rec->value_type == SS_SDK_VALUE_STR && rec->value.str == NULL) {
        return -1;
    }

    if (rec->value_type == SS_SDK_VALUE_F64 && !isfinite(rec->value.f64)) {
        return -1;
    }

    return 0;
}

static int ss_parse_f64_bits(const char *s, double *out)
{
    uint64_t bits;
    char *end;

    /* BUGFIX(#38): canonical float format is raw IEEE-754 bits in hex. */
    if (strncmp(s, "0x", 2) != 0 && strncmp(s, "0X", 2) != 0) {
        return -1;
    }

    errno = 0;
    bits = strtoull(s + 2, &end, 16);
    if (errno != 0 || end == s + 2 || *end != '\0') {
        return -1;
    }

    memcpy(out, &bits, sizeof(bits));
    return 0;
}

static uint64_t ss_f64_to_bits(double value)
{
    uint64_t bits = 0;
    memcpy(&bits, &value, sizeof(bits));
    return bits;
}

static int ss_parse_line(char *line, ss_sdk_record *out, bool duplicate_strings);

static int ss_cmp_i64(int64_t a, int64_t b)
{
    if (a < b) {
        return -1;
    }
    if (a > b) {
        return 1;
    }
    return 0;
}

static int ss_cmp_u64(uint64_t a, uint64_t b)
{
    if (a < b) {
        return -1;
    }
    if (a > b) {
        return 1;
    }
    return 0;
}

static int ss_cmp_int(int a, int b)
{
    if (a < b) {
        return -1;
    }
    if (a > b) {
        return 1;
    }
    return 0;
}

static int ss_cmp_nullable_str(const char *a, const char *b)
{
    const char *x = (a == NULL) ? "" : a;
    const char *y = (b == NULL) ? "" : b;
    const int cmp = strcmp(x, y);

    if (cmp < 0) {
        return -1;
    }
    if (cmp > 0) {
        return 1;
    }
    return 0;
}

static int ss_cmp_bool(bool a, bool b)
{
    if (a == b) {
        return 0;
    }
    return a ? 1 : -1;
}

static int ss_record_value_cmp(const ss_sdk_record *a, const ss_sdk_record *b)
{
    int cmp;

    switch (a->value_type) {
        case SS_SDK_VALUE_I64:
            return ss_cmp_i64(a->value.i64, b->value.i64);
        case SS_SDK_VALUE_F64:
            return ss_cmp_u64(ss_f64_to_bits(a->value.f64), ss_f64_to_bits(b->value.f64));
        case SS_SDK_VALUE_BOOL:
            return ss_cmp_bool(a->value.boolean, b->value.boolean);
        case SS_SDK_VALUE_STR:
            return ss_cmp_nullable_str(a->value.str, b->value.str);
        default:
            cmp = 0;
            break;
    }

    return cmp;
}

static char *ss_next_line(char **cursor)
{
    char *line;
    char *nl;

    if (cursor == NULL || *cursor == NULL || **cursor == '\0') {
        return NULL;
    }

    line = *cursor;
    nl = strchr(line, '\n');
    if (nl != NULL) {
        *nl = '\0';
        *cursor = nl + 1;
    } else {
        *cursor = NULL;
    }

    return line;
}

static void ss_free_record_array(ss_sdk_record *records, size_t count)
{
    size_t i;

    if (records == NULL) {
        return;
    }
    for (i = 0; i < count; ++i) {
        ss_record_free_strings(&records[i]);
    }
    free(records);
}

static int ss_append_owned_record(
    ss_sdk_record **records,
    size_t *count,
    size_t *cap,
    const ss_sdk_record *record)
{
    ss_sdk_record *grown;
    size_t next_cap;

    if (*count == *cap) {
        next_cap = (*cap == 0) ? 64 : (*cap * 2);
        if (next_cap < *cap || next_cap > SIZE_MAX / sizeof(ss_sdk_record)) {
            errno = EOVERFLOW;
            return -1;
        }
        grown = (ss_sdk_record *)realloc(*records, next_cap * sizeof(ss_sdk_record));
        if (grown == NULL) {
            return -1;
        }
        *records = grown;
        *cap = next_cap;
    }

    (*records)[*count] = *record;
    *count += 1;
    return 0;
}

static int ss_collect_records_in_window(char *all, int64_t from_ts, ss_sdk_record **out_records, size_t *out_count)
{
    char *p = all;
    char *line;
    ss_sdk_record *tmp = NULL;
    size_t cap = 0;
    size_t count = 0;

    while ((line = ss_next_line(&p)) != NULL) {
        ss_sdk_record rec;

        if (ss_parse_line(line, &rec, true) != 0) {
            continue;
        }

        if (rec.ts_start_utc < from_ts) {
            ss_record_free_strings(&rec);
            continue;
        }

        if (ss_append_owned_record(&tmp, &count, &cap, &rec) != 0) {
            ss_record_free_strings(&rec);
            ss_free_record_array(tmp, count);
            return -1;
        }
    }

    *out_records = tmp;
    *out_count = count;
    return 0;
}

static int ss_parse_line(char *line, ss_sdk_record *out, bool duplicate_strings)
{
    char *fields[SS_DB_FIELD_COUNT];
    int parsed;
    int tmp_int;
    int64_t tmp_i64;

    ss_record_reset(out);

    parsed = ss_split_fields(line, fields);
    if (parsed != SS_DB_FIELD_COUNT) {
        return -1;
    }

    for (tmp_int = 0; tmp_int < SS_DB_FIELD_COUNT; ++tmp_int) {
        ss_unescape_inplace(fields[tmp_int]);
    }

    if (ss_parse_int(fields[0], &tmp_int) != 0) {
        return -1;
    }
    out->metric = (ss_metric_id)tmp_int;
    /* BUGFIX(#33): reject invalid enum values from on-disk corruption. */
    if (!ss_is_valid_metric(out->metric)) {
        return -1;
    }

    if (ss_parse_int(fields[1], &tmp_int) != 0) {
        return -1;
    }
    out->value_type = (ss_sdk_value_type)tmp_int;
    /* BUGFIX(#33): reject invalid enum values from on-disk corruption. */
    if (!ss_is_valid_value_type(out->value_type)) {
        return -1;
    }

    if (ss_parse_i64(fields[3], &tmp_i64) != 0) {
        return -1;
    }
    out->ts_start_utc = tmp_i64;

    if (ss_parse_i64(fields[4], &tmp_i64) != 0) {
        return -1;
    }
    out->ts_end_utc = tmp_i64;

    if (ss_parse_int(fields[5], &tmp_int) != 0) {
        return -1;
    }
    out->data_kind = (ss_sdk_data_kind)tmp_int;
    /* BUGFIX(#33): reject invalid enum values from on-disk corruption. */
    if (!ss_is_valid_data_kind(out->data_kind)) {
        return -1;
    }

    if (ss_parse_i64(fields[10], &tmp_i64) != 0) {
        return -1;
    }
    out->model_run_utc = tmp_i64;

    if (ss_parse_i64(fields[11], &tmp_i64) != 0) {
        return -1;
    }
    out->issued_at_utc = tmp_i64;

    switch (out->value_type) {
        case SS_SDK_VALUE_I64:
            if (ss_parse_i64(fields[2], &out->value.i64) != 0) {
                return -1;
            }
            break;
        case SS_SDK_VALUE_F64:
            /* BUGFIX(#38): accept canonical bit format + legacy decimal fallback. */
            if (ss_parse_f64_bits(fields[2], &out->value.f64) != 0 &&
                ss_parse_double(fields[2], &out->value.f64) != 0) {
                return -1;
            }
            break;
        case SS_SDK_VALUE_BOOL:
            if (strcmp(fields[2], "1") == 0 || strcmp(fields[2], "true") == 0) {
                out->value.boolean = true;
            } else if (strcmp(fields[2], "0") == 0 || strcmp(fields[2], "false") == 0) {
                out->value.boolean = false;
            } else {
                return -1;
            }
            break;
        case SS_SDK_VALUE_STR:
            if (duplicate_strings) {
                out->value.str = ss_strdup_local(fields[2]);
                if (out->value.str == NULL) {
                    return -1;
                }
            } else {
                out->value.str = fields[2];
            }
            break;
        default:
            return -1;
    }

    if (duplicate_strings) {
        out->source_api = ss_strdup_local(fields[6]);
        out->source_field = ss_strdup_local(fields[7]);
        out->source_tz = ss_strdup_local(fields[8]);
        out->model_id = ss_strdup_local(fields[9]);

        if (out->source_api == NULL || out->source_field == NULL || out->source_tz == NULL || out->model_id == NULL) {
            ss_record_free_strings(out);
            return -1;
        }
    } else {
        out->source_api = fields[6];
        out->source_field = fields[7];
        out->source_tz = fields[8];
        out->model_id = fields[9];
    }

    if (ss_validate_parsed_record(out) != 0) {
        if (duplicate_strings) {
            ss_record_free_strings(out);
        }
        return -1;
    }

    return 0;
}

static bool ss_string_equal_nullable(const char *a, const char *b)
{
    const char *x = (a == NULL) ? "" : a;
    const char *y = (b == NULL) ? "" : b;
    return strcmp(x, y) == 0;
}

static bool ss_record_identity_equal(const ss_sdk_record *a, const ss_sdk_record *b)
{
    /* BUGFIX(#28): dedupe on full logical identity, not a weak partial key. */
    if (a->metric != b->metric ||
        a->value_type != b->value_type ||
        a->ts_start_utc != b->ts_start_utc ||
        a->ts_end_utc != b->ts_end_utc ||
        a->data_kind != b->data_kind ||
        a->model_run_utc != b->model_run_utc ||
        a->issued_at_utc != b->issued_at_utc ||
        !ss_string_equal_nullable(a->source_api, b->source_api) ||
        !ss_string_equal_nullable(a->source_field, b->source_field) ||
        !ss_string_equal_nullable(a->source_tz, b->source_tz) ||
        !ss_string_equal_nullable(a->model_id, b->model_id)) {
        return false;
    }

    switch (a->value_type) {
        case SS_SDK_VALUE_I64:
            return a->value.i64 == b->value.i64;
        case SS_SDK_VALUE_F64:
            /* Compare float payload by bits so -0.0 and +0.0 stay distinct. */
            return ss_f64_to_bits(a->value.f64) == ss_f64_to_bits(b->value.f64);
        case SS_SDK_VALUE_BOOL:
            return a->value.boolean == b->value.boolean;
        case SS_SDK_VALUE_STR:
            return ss_string_equal_nullable(a->value.str, b->value.str);
        default:
            return false;
    }
}

static char *ss_record_to_line(const ss_sdk_record *rec)
{
    char i64_buf[32];
    char f64_buf[64];
    char model_run_buf[32];
    char issued_at_buf[32];
    char *e_value = NULL;
    char *e_source_api = NULL;
    char *e_source_field = NULL;
    char *e_source_tz = NULL;
    char *e_model_id = NULL;
    char *line = NULL;
    int n;

    switch (rec->value_type) {
        case SS_SDK_VALUE_I64:
            snprintf(i64_buf, sizeof(i64_buf), "%lld", (long long)rec->value.i64);
            e_value = ss_escape(i64_buf);
            break;
        case SS_SDK_VALUE_F64:
            /* BUGFIX(#38): write locale-stable float encoding. */
            snprintf(f64_buf, sizeof(f64_buf), "0x%016" PRIx64, ss_f64_to_bits(rec->value.f64));
            e_value = ss_escape(f64_buf);
            break;
        case SS_SDK_VALUE_BOOL:
            e_value = ss_escape(rec->value.boolean ? "1" : "0");
            break;
        case SS_SDK_VALUE_STR:
            e_value = ss_escape(rec->value.str == NULL ? "" : rec->value.str);
            break;
        default:
            return NULL;
    }

    e_source_api = ss_escape(rec->source_api == NULL ? "" : rec->source_api);
    e_source_field = ss_escape(rec->source_field == NULL ? "" : rec->source_field);
    e_source_tz = ss_escape(rec->source_tz == NULL ? "" : rec->source_tz);
    e_model_id = ss_escape(rec->model_id == NULL ? "" : rec->model_id);

    if (e_value == NULL || e_source_api == NULL || e_source_field == NULL || e_source_tz == NULL || e_model_id == NULL) {
        goto cleanup;
    }

    snprintf(model_run_buf, sizeof(model_run_buf), "%lld", (long long)rec->model_run_utc);
    snprintf(issued_at_buf, sizeof(issued_at_buf), "%lld", (long long)rec->issued_at_utc);

    n = snprintf(
        NULL,
        0,
        "%d\t%d\t%s\t%lld\t%lld\t%d\t%s\t%s\t%s\t%s\t%s\t%s\n",
        (int)rec->metric,
        (int)rec->value_type,
        e_value,
        (long long)rec->ts_start_utc,
        (long long)rec->ts_end_utc,
        (int)rec->data_kind,
        e_source_api,
        e_source_field,
        e_source_tz,
        e_model_id,
        model_run_buf,
        issued_at_buf);

    if (n < 0) {
        goto cleanup;
    }

    line = (char *)malloc((size_t)n + 1U);
    if (line == NULL) {
        goto cleanup;
    }
    snprintf(
        line,
        (size_t)n + 1U,
        "%d\t%d\t%s\t%lld\t%lld\t%d\t%s\t%s\t%s\t%s\t%s\t%s\n",
        (int)rec->metric,
        (int)rec->value_type,
        e_value,
        (long long)rec->ts_start_utc,
        (long long)rec->ts_end_utc,
        (int)rec->data_kind,
        e_source_api,
        e_source_field,
        e_source_tz,
        e_model_id,
        model_run_buf,
        issued_at_buf);

cleanup:
    free(e_value);
    free(e_source_api);
    free(e_source_field);
    free(e_source_tz);
    free(e_model_id);
    return line;
}

static int ss_record_cmp(const void *lhs, const void *rhs)
{
    const ss_sdk_record *a = (const ss_sdk_record *)lhs;
    const ss_sdk_record *b = (const ss_sdk_record *)rhs;
    int cmp;

    cmp = ss_cmp_i64(a->ts_start_utc, b->ts_start_utc);
    if (cmp != 0) return cmp;
    cmp = ss_cmp_i64(a->ts_end_utc, b->ts_end_utc);
    if (cmp != 0) return cmp;
    cmp = ss_cmp_i64(a->issued_at_utc, b->issued_at_utc);
    if (cmp != 0) return cmp;
    cmp = ss_cmp_i64(a->model_run_utc, b->model_run_utc);
    if (cmp != 0) return cmp;
    cmp = ss_cmp_int((int)a->data_kind, (int)b->data_kind);
    if (cmp != 0) return cmp;
    cmp = ss_cmp_int((int)a->metric, (int)b->metric);
    if (cmp != 0) return cmp;
    cmp = ss_cmp_int((int)a->value_type, (int)b->value_type);
    if (cmp != 0) return cmp;

    cmp = ss_cmp_nullable_str(a->source_api, b->source_api);
    if (cmp != 0) return cmp;
    cmp = ss_cmp_nullable_str(a->source_field, b->source_field);
    if (cmp != 0) return cmp;
    cmp = ss_cmp_nullable_str(a->source_tz, b->source_tz);
    if (cmp != 0) return cmp;
    cmp = ss_cmp_nullable_str(a->model_id, b->model_id);
    if (cmp != 0) return cmp;

    return ss_record_value_cmp(a, b);
}

static int ss_line_is_duplicate(char *all, const ss_sdk_record *record)
{
    char *p = all;
    char *line;
    ss_sdk_record parsed;

    /* v1 dedupe strategy: linear scan across file snapshot under write lock. */
    while ((line = ss_next_line(&p)) != NULL) {
        if (ss_parse_line(line, &parsed, false) == 0) {
            if (ss_record_identity_equal(&parsed, record)) {
                return 1;
            }
        }
    }

    return 0;
}

ss_sdk_status ss_sdk_internal_db_write_record(const ss_sdk_record *record)
{
    const char *path = ss_db_path();
    int fd;
    char *existing = NULL;
    size_t existing_size = 0;
    char *line = NULL;
    int is_dup;
    ss_sdk_status rc = SS_SDK_ERR_INTERNAL;

    if (ss_ensure_parent_dirs(path) != 0) {
        return SS_SDK_ERR_INTERNAL;
    }

    fd = open(path, O_RDWR | O_CREAT, 0664);
    if (fd < 0) {
        return SS_SDK_ERR_INTERNAL;
    }

    /* Whole-file exclusive lock: simple and safe for v1 file backend. */
    if (flock(fd, LOCK_EX) != 0) {
        close(fd);
        return SS_SDK_ERR_INTERNAL;
    }

    existing = ss_read_all_fd(fd, &existing_size);
    if (existing == NULL) {
        existing = ss_strdup_local("");
        if (existing == NULL) {
            goto done;
        }
    }

    is_dup = ss_line_is_duplicate(existing, record);
    if (is_dup) {
        rc = SS_SDK_OK;
        goto done;
    }

    line = ss_record_to_line(record);
    if (line == NULL) {
        goto done;
    }

    if (lseek(fd, 0, SEEK_END) < 0) {
        goto done;
    }
    if (ss_write_all(fd, line, strlen(line)) != 0) {
        goto done;
    }
    /* BUGFIX(#35): DB write acknowledgement is durable by default. */
    if (fsync(fd) != 0) {
        goto done;
    }

    rc = SS_SDK_OK;

done:
    free(line);
    free(existing);
    flock(fd, LOCK_UN);
    close(fd);
    (void)existing_size;
    return rc;
}

ss_sdk_status ss_sdk_internal_db_get_last_weeks(int weeks, ss_sdk_record **out_records, size_t *out_count)
{
    const char *path = ss_db_path();
    int fd;
    char *all = NULL;
    size_t size = 0;
    ss_sdk_record *tmp = NULL;
    size_t count = 0;
    int eff_weeks;
    time_t now;
    int64_t from_ts;
    ss_db_result_block *block;

    *out_records = NULL;
    *out_count = 0;

    eff_weeks = weeks;
    if (eff_weeks > SS_SDK_DB_MAX_WEEKS) {
        eff_weeks = SS_SDK_DB_MAX_WEEKS;
    }

    now = time(NULL);
    if (eff_weeks == 0) {
        from_ts = (int64_t)now;
    } else {
        from_ts = (int64_t)now - (int64_t)eff_weeks * 7 * 24 * 3600;
    }

    fd = open(path, O_RDONLY);
    if (fd < 0) {
        if (errno == ENOENT) {
            return SS_SDK_OK;
        }
        return SS_SDK_ERR_INTERNAL;
    }

    /* Shared lock gives a consistent snapshot while parsing rows. */
    if (flock(fd, LOCK_SH) != 0) {
        close(fd);
        return SS_SDK_ERR_INTERNAL;
    }

    all = ss_read_all_fd(fd, &size);
    flock(fd, LOCK_UN);
    close(fd);
    if (all == NULL) {
        return SS_SDK_ERR_INTERNAL;
    }

    if (ss_collect_records_in_window(all, from_ts, &tmp, &count) != 0) {
        free(all);
        return SS_SDK_ERR_INTERNAL;
    }
    free(all);

    if (count == 0) {
        free(tmp);
        return SS_SDK_OK;
    }

    qsort(tmp, count, sizeof(ss_sdk_record), ss_record_cmp);

    block = (ss_db_result_block *)malloc(sizeof(ss_db_result_block) + count * sizeof(ss_sdk_record));
    if (block == NULL) {
        ss_free_record_array(tmp, count);
        return SS_SDK_ERR_INTERNAL;
    }

    block->count = count;
    memcpy(block->records, tmp, count * sizeof(ss_sdk_record));
    free(tmp);

    *out_records = block->records;
    *out_count = count;
    (void)size;
    return SS_SDK_OK;
}

void ss_sdk_internal_db_free_records(ss_sdk_record *records)
{
    size_t i;
    ss_db_result_block *block;

    if (records == NULL) {
        return;
    }

    block = ((ss_db_result_block *)records) - 1;
    for (i = 0; i < block->count; ++i) {
        ss_record_free_strings(&block->records[i]);
    }
    free(block);
}
