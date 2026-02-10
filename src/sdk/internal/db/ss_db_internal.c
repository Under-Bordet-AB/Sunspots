#include "sdk/internal/db/ss_db_internal.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
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
            n += 2;
        } else {
            n += 1;
        }
    }

    out = (char *)malloc(n + 1U);
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
    char *end;
    double v;

    errno = 0;
    v = strtod(s, &end);
    if (errno != 0 || end == s || *end != '\0') {
        return -1;
    }
    *out = v;
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

    if (ss_parse_int(fields[1], &tmp_int) != 0) {
        return -1;
    }
    out->value_type = (ss_sdk_value_type)tmp_int;

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
            if (ss_parse_double(fields[2], &out->value.f64) != 0) {
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
    return a->metric == b->metric &&
           a->ts_start_utc == b->ts_start_utc &&
           a->data_kind == b->data_kind &&
           a->issued_at_utc == b->issued_at_utc &&
           ss_string_equal_nullable(a->model_id, b->model_id) &&
           ss_string_equal_nullable(a->source_api, b->source_api);
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
            snprintf(f64_buf, sizeof(f64_buf), "%.17g", rec->value.f64);
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

    if (a->ts_start_utc < b->ts_start_utc) {
        return -1;
    }
    if (a->ts_start_utc > b->ts_start_utc) {
        return 1;
    }
    if (a->issued_at_utc < b->issued_at_utc) {
        return -1;
    }
    if (a->issued_at_utc > b->issued_at_utc) {
        return 1;
    }
    if (a->metric < b->metric) {
        return -1;
    }
    if (a->metric > b->metric) {
        return 1;
    }
    return 0;
}

static int ss_line_is_duplicate(char *all, const ss_sdk_record *record)
{
    char *p = all;
    ss_sdk_record parsed;

    /* v1 dedupe strategy: linear scan across file snapshot under write lock. */
    while (p != NULL && *p != '\0') {
        char *line = p;
        char *nl = strchr(p, '\n');
        if (nl != NULL) {
            *nl = '\0';
            p = nl + 1;
        } else {
            p = NULL;
        }

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
    char *p;
    ss_sdk_record *tmp = NULL;
    size_t cap = 0;
    size_t count = 0;
    int eff_weeks;
    time_t now;
    int64_t from_ts;
    size_t i;
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

    p = all;
    while (p != NULL && *p != '\0') {
        char *line = p;
        char *nl = strchr(p, '\n');
        ss_sdk_record rec;

        if (nl != NULL) {
            *nl = '\0';
            p = nl + 1;
        } else {
            p = NULL;
        }

        if (ss_parse_line(line, &rec, true) == 0) {
            if (rec.ts_start_utc >= from_ts) {
                if (count == cap) {
                    size_t next_cap = (cap == 0) ? 64 : cap * 2;
                    ss_sdk_record *grown = (ss_sdk_record *)realloc(tmp, next_cap * sizeof(ss_sdk_record));
                    if (grown == NULL) {
                        ss_record_free_strings(&rec);
                        for (i = 0; i < count; ++i) {
                            ss_record_free_strings(&tmp[i]);
                        }
                        free(tmp);
                        free(all);
                        return SS_SDK_ERR_INTERNAL;
                    }
                    tmp = grown;
                    cap = next_cap;
                }
                tmp[count++] = rec;
            } else {
                ss_record_free_strings(&rec);
            }
        }

    }

    free(all);

    if (count == 0) {
        free(tmp);
        return SS_SDK_OK;
    }

    qsort(tmp, count, sizeof(ss_sdk_record), ss_record_cmp);

    block = (ss_db_result_block *)malloc(sizeof(ss_db_result_block) + count * sizeof(ss_sdk_record));
    if (block == NULL) {
        for (i = 0; i < count; ++i) {
            ss_record_free_strings(&tmp[i]);
        }
        free(tmp);
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
