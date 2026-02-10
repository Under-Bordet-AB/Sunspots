#include "fetch/fetch_engine.h"

#include <curl/curl.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <math.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "core/runtime_supervision.h"
#include "libs/json/cJSON.h"
#include "sdk/ss_sdk.h"

static volatile sig_atomic_t g_running = 1;

typedef enum json_expect_type {
    JSON_EXPECT_ANY = 0,
    JSON_EXPECT_NUMBER,
    JSON_EXPECT_STRING,
    JSON_EXPECT_BOOL,
    JSON_EXPECT_ARRAY,
    JSON_EXPECT_OBJECT,
    JSON_EXPECT_NULL
} json_expect_type;

typedef struct node_list {
    cJSON** items;
    size_t len;
    size_t cap;
} node_list;

static void on_term(int sig) {
    (void)sig;
    g_running = 0;
}

static int node_list_push(node_list* list, cJSON* node) {
    if (!list || !node) {
        return -EINVAL;
    }
    if (list->len == list->cap) {
        size_t new_cap = (list->cap == 0) ? 8 : (list->cap * 2);
        cJSON** p = realloc(list->items, new_cap * sizeof(*p));
        if (!p) {
            return -ENOMEM;
        }
        list->items = p;
        list->cap = new_cap;
    }
    list->items[list->len++] = node;
    return 0;
}

static int format_source_field_indexed(const char* template_src, size_t idx, char* out, size_t out_sz) {
    if (!template_src || !out || out_sz == 0) {
        return -EINVAL;
    }

    size_t oi = 0;
    for (size_t i = 0; template_src[i] != '\0'; i++) {
        if (template_src[i] == '[' && template_src[i + 1] == '*' && template_src[i + 2] == ']') {
            int n = snprintf(out + oi, out_sz - oi, "[%zu]", idx);
            if (n <= 0 || (size_t)n >= (out_sz - oi)) {
                return -ENAMETOOLONG;
            }
            oi += (size_t)n;
            i += 2;
            continue;
        }
        if (oi + 2 > out_sz) {
            return -ENAMETOOLONG;
        }
        out[oi++] = template_src[i];
    }
    out[oi] = '\0';
    return 0;
}

static void node_list_free(node_list* list) {
    if (!list) {
        return;
    }
    free(list->items);
    list->items = NULL;
    list->len = 0;
    list->cap = 0;
}

static int parse_parent_pid(int argc, char** argv, pid_t* out_parent_pid) {
    if (argc < 2 || !argv || !out_parent_pid) {
        return -EINVAL;
    }
    errno = 0;
    char* endptr = NULL;
    long parent_pid = strtol(argv[1], &endptr, 10);
    if (errno != 0 || endptr == argv[1] || *endptr != '\0' || parent_pid <= 1) {
        return -EINVAL;
    }
    *out_parent_pid = (pid_t)parent_pid;
    return 0;
}

typedef struct curl_fixed_buffer {
    char* data;
    size_t len;
    size_t cap;
} curl_fixed_buffer;

static int read_file_dynamic(const char* path, char** out, size_t* out_len) {
    if (!path || !out || !out_len) {
        return -EINVAL;
    }

    FILE* f = fopen(path, "rb");
    if (!f) {
        return -errno;
    }

    size_t cap = 4096;
    size_t len = 0;
    char* buf = malloc(cap);
    if (!buf) {
        (void)fclose(f);
        return -ENOMEM;
    }

    char chunk[4096];
    while (!feof(f)) {
        size_t n = fread(chunk, 1, sizeof(chunk), f);
        if (n == 0) {
            break;
        }
        if (len > SIZE_MAX - n - 1) {
            free(buf);
            (void)fclose(f);
            return -EOVERFLOW;
        }
        size_t need = len + n + 1;
        if (need > cap) {
            size_t new_cap = cap;
            while (new_cap < need) {
                if (new_cap > SIZE_MAX / 2) {
                    new_cap = need;
                    break;
                }
                new_cap *= 2;
            }
            char* grown = realloc(buf, new_cap);
            if (!grown) {
                free(buf);
                (void)fclose(f);
                return -ENOMEM;
            }
            buf = grown;
            cap = new_cap;
        }
        memcpy(buf + len, chunk, n);
        len += n;
    }
    if (ferror(f)) {
        free(buf);
        (void)fclose(f);
        return -EIO;
    }
    buf[len] = '\0';
    (void)fclose(f);

    *out = buf;
    *out_len = len;
    return (int)len;
}

static size_t curl_write_cb(char* ptr, size_t size, size_t nmemb, void* userdata) {
    curl_fixed_buffer* b = (curl_fixed_buffer*)userdata;
    size_t in_sz = size * nmemb;
    if (!b || !ptr || in_sz == 0) {
        return 0;
    }
    if (b->len > SIZE_MAX - in_sz - 1) {
        return 0;
    }

    size_t need = b->len + in_sz + 1;
    if (need > b->cap) {
        size_t new_cap = (b->cap == 0) ? 4096 : b->cap;
        while (new_cap < need) {
            if (new_cap > SIZE_MAX / 2) {
                new_cap = need;
                break;
            }
            new_cap *= 2;
        }
        char* grown = realloc(b->data, new_cap);
        if (!grown) {
            return 0;
        }
        b->data = grown;
        b->cap = new_cap;
    }
    memcpy(b->data + b->len, ptr, in_sz);
    b->len += in_sz;
    b->data[b->len] = '\0';
    return in_sz;
}

static int fetch_text(CURL* easy, const char* url, char** out_body, size_t* out_len) {
    if (!url || !out_body || !out_len) {
        return -EINVAL;
    }
    *out_body = NULL;
    *out_len = 0;

    if (strncmp(url, "file://", 7) == 0) {
        return read_file_dynamic(url + 7, out_body, out_len);
    }
    if (!easy) {
        return -EINVAL;
    }

    curl_fixed_buffer buf = {.data = NULL, .len = 0, .cap = 0};

    curl_easy_setopt(easy, CURLOPT_URL, url);
    curl_easy_setopt(easy, CURLOPT_WRITEDATA, &buf);
    CURLcode rc = curl_easy_perform(easy);
    if (rc != CURLE_OK) {
        free(buf.data);
        return -EIO;
    }

    if (!buf.data) {
        buf.data = calloc(1, 1);
        if (!buf.data) {
            return -ENOMEM;
        }
        buf.len = 0;
        buf.cap = 1;
    }
    *out_body = buf.data;
    *out_len = buf.len;
    return (int)buf.len;
}

static void sleep_until_utc(time_t target_utc) {
    while (g_running) {
        time_t now = time(NULL);
        if (now >= target_utc) {
            return;
        }
        time_t delta = target_utc - now;
        struct timespec ts = {.tv_sec = delta > 1 ? 1 : delta, .tv_nsec = 0};
        (void)nanosleep(&ts, NULL);
    }
}

static void sleep_seconds_interruptible(int seconds) {
    if (seconds <= 0) {
        return;
    }
    while (g_running && seconds > 0) {
        struct timespec ts = {.tv_sec = seconds > 1 ? 1 : seconds, .tv_nsec = 0};
        (void)nanosleep(&ts, NULL);
        seconds--;
    }
}

static int ensure_status_dirs(void) {
    if (mkdir("runtime", 0755) != 0 && errno != EEXIST) {
        return -errno;
    }
    if (mkdir("runtime/status", 0755) != 0 && errno != EEXIST) {
        return -errno;
    }
    return 0;
}

static int clamp_positive_or_default(int value, int fallback) {
    if (value <= 0 || value > INT_MAX / 2) {
        return fallback;
    }
    return value;
}

static int write_slot_status(const char* status_path, const char* worker_name, time_t slot_start_utc,
                             time_t slot_deadline_utc, bool ok, int records_written, const char* error) {
    ss_worker_status status;
    memset(&status, 0, sizeof(status));
    (void)snprintf(status.worker, sizeof(status.worker), "%s", worker_name ? worker_name : "");
    status.slot_start_utc = slot_start_utc;
    status.slot_deadline_utc = slot_deadline_utc;
    status.completed_at_utc = time(NULL);
    status.ok = ok;
    status.records_written = records_written;
    (void)snprintf(status.error, sizeof(status.error), "%s", error ? error : "");
    return ss_status_write_atomic(status_path, &status);
}

static const ss_metric_meta* metric_meta_by_name(const char* canonical_name) {
    if (!canonical_name || canonical_name[0] == '\0') {
        return NULL;
    }
    for (int i = 0; i < SS_METRIC_COUNT; i++) {
        const ss_metric_meta* m = ss_metric_meta_get((ss_metric_id)i);
        if (!m || !m->canonical_name) {
            continue;
        }
        if (strcmp(m->canonical_name, canonical_name) == 0) {
            return m;
        }
    }
    return NULL;
}

static ss_sdk_value_transform parse_transform(const char* s) {
    if (!s || s[0] == '\0' || strcmp(s, "none") == 0) {
        return SS_SDK_XFORM_NONE;
    }
    if (strcmp(s, "f_to_c") == 0) {
        return SS_SDK_XFORM_F_TO_C;
    }
    if (strcmp(s, "kph_to_ms") == 0) {
        return SS_SDK_XFORM_KPH_TO_MS;
    }
    if (strcmp(s, "mph_to_ms") == 0) {
        return SS_SDK_XFORM_MPH_TO_MS;
    }
    return SS_SDK_XFORM_NONE;
}

static json_expect_type parse_expect_type(const char* s) {
    if (!s || s[0] == '\0' || strcmp(s, "any") == 0) {
        return JSON_EXPECT_ANY;
    }
    if (strcmp(s, "number") == 0) {
        return JSON_EXPECT_NUMBER;
    }
    if (strcmp(s, "string") == 0) {
        return JSON_EXPECT_STRING;
    }
    if (strcmp(s, "bool") == 0 || strcmp(s, "boolean") == 0) {
        return JSON_EXPECT_BOOL;
    }
    if (strcmp(s, "array") == 0) {
        return JSON_EXPECT_ARRAY;
    }
    if (strcmp(s, "object") == 0) {
        return JSON_EXPECT_OBJECT;
    }
    if (strcmp(s, "null") == 0) {
        return JSON_EXPECT_NULL;
    }
    return JSON_EXPECT_ANY;
}

static json_expect_type infer_expect_from_metric_type(ss_sdk_value_type metric_type) {
    switch (metric_type) {
        case SS_SDK_VALUE_I64:
        case SS_SDK_VALUE_F64:
            return JSON_EXPECT_NUMBER;
        case SS_SDK_VALUE_STR:
            return JSON_EXPECT_STRING;
        case SS_SDK_VALUE_BOOL:
            return JSON_EXPECT_BOOL;
        default:
            return JSON_EXPECT_ANY;
    }
}

static bool json_matches_expect(cJSON* node, json_expect_type expect) {
    if (!node) {
        return false;
    }
    switch (expect) {
        case JSON_EXPECT_ANY:
            return true;
        case JSON_EXPECT_NUMBER:
            return cJSON_IsNumber(node);
        case JSON_EXPECT_STRING:
            return cJSON_IsString(node);
        case JSON_EXPECT_BOOL:
            return cJSON_IsBool(node);
        case JSON_EXPECT_ARRAY:
            return cJSON_IsArray(node);
        case JSON_EXPECT_OBJECT:
            return cJSON_IsObject(node);
        case JSON_EXPECT_NULL:
            return cJSON_IsNull(node);
        default:
            return false;
    }
}

static bool parse_bool_text(const char* s, bool* out) {
    if (!s || !out) {
        return false;
    }
    if (strcasecmp(s, "true") == 0 || strcmp(s, "1") == 0) {
        *out = true;
        return true;
    }
    if (strcasecmp(s, "false") == 0 || strcmp(s, "0") == 0) {
        *out = false;
        return true;
    }
    return false;
}

static bool parse_i64_text(const char* s, int64_t* out) {
    if (!s || !out) {
        return false;
    }
    errno = 0;
    char* end = NULL;
    long long v = strtoll(s, &end, 10);
    if (errno != 0 || end == s || *end != '\0') {
        return false;
    }
    *out = (int64_t)v;
    return true;
}

static bool parse_f64_text(const char* s, double* out) {
    if (!s || !out) {
        return false;
    }
    errno = 0;
    char* end = NULL;
    double v = strtod(s, &end);
    if (errno != 0 || end == s || *end != '\0') {
        return false;
    }
    *out = v;
    return true;
}

static bool parse_iso8601_utc(const char* s, time_t* out_ts) {
    if (!s || !out_ts) {
        return false;
    }
    struct tm tmv;
    memset(&tmv, 0, sizeof(tmv));
    char* rc = strptime(s, "%Y-%m-%dT%H:%M:%SZ", &tmv);
    if (!rc || *rc != '\0') {
        return false;
    }
    time_t ts = timegm(&tmv);
    if (ts <= 0) {
        return false;
    }
    *out_ts = ts;
    return true;
}

static bool json_to_timestamp(cJSON* node, int64_t* out_ts) {
    if (!node || !out_ts) {
        return false;
    }
    if (cJSON_IsNumber(node)) {
        double v = node->valuedouble;
        if (v <= 0) {
            return false;
        }
        *out_ts = (int64_t)v;
        return true;
    }
    if (cJSON_IsString(node) && node->valuestring) {
        int64_t i = 0;
        if (parse_i64_text(node->valuestring, &i) && i > 0) {
            *out_ts = i;
            return true;
        }
        time_t ts;
        if (parse_iso8601_utc(node->valuestring, &ts)) {
            *out_ts = (int64_t)ts;
            return true;
        }
    }
    return false;
}

typedef struct emit_context {
    const char* source_api;
    const char* source_tz;
    const char* forecast_model_id;
    int64_t forecast_model_run_utc;
    int64_t forecast_issued_at_utc;
} emit_context;

static int apply_f64_transform(double* value, ss_sdk_value_transform xform) {
    if (!value) {
        return -EINVAL;
    }
    switch (xform) {
        case SS_SDK_XFORM_NONE:
            return 0;
        case SS_SDK_XFORM_F_TO_C:
            *value = (*value - 32.0) * (5.0 / 9.0);
            return 0;
        case SS_SDK_XFORM_KPH_TO_MS:
            *value = *value / 3.6;
            return 0;
        case SS_SDK_XFORM_MPH_TO_MS:
            *value = *value * 0.44704;
            return 0;
        default:
            return -EINVAL;
    }
}

static int sdk_write_node(const emit_context* ctx, const ss_metric_meta* meta, cJSON* node, int64_t ts_utc,
                          const char* source_field, ss_sdk_value_transform transform, bool coerce, bool has_scale,
                          double scale, double offset) {
    if (!ctx || !meta || !node || ts_utc <= 0 || !source_field || source_field[0] == '\0' || !ctx->source_api ||
        ctx->source_api[0] == '\0' || !ctx->source_tz || ctx->source_tz[0] == '\0') {
        return -EINVAL;
    }

    ss_sdk_record rec;
    memset(&rec, 0, sizeof(rec));
    rec.metric = meta->id;
    rec.value_type = meta->value_type;
    rec.ts_start_utc = ts_utc;
    rec.ts_end_utc = ts_utc;
    rec.source_api = ctx->source_api;
    rec.source_field = source_field;
    rec.source_tz = ctx->source_tz;

    const int64_t now_utc = (int64_t)time(NULL);
    if (ts_utc > now_utc) {
        rec.data_kind = SS_SDK_DATA_FORECAST;
        rec.model_id = (ctx->forecast_model_id && ctx->forecast_model_id[0] != '\0') ? ctx->forecast_model_id
                                                                                      : ctx->source_api;
        rec.model_run_utc = ctx->forecast_model_run_utc > 0 ? ctx->forecast_model_run_utc : ts_utc;
        rec.issued_at_utc = ctx->forecast_issued_at_utc > 0 ? ctx->forecast_issued_at_utc : ts_utc;
    } else {
        rec.data_kind = SS_SDK_DATA_OBSERVATION;
        rec.model_id = "";
        rec.model_run_utc = 0;
        rec.issued_at_utc = 0;
    }

    if (meta->value_type == SS_SDK_VALUE_F64) {
        double v = 0.0;
        if (cJSON_IsNumber(node)) {
            v = node->valuedouble;
        } else if (coerce && cJSON_IsString(node) && node->valuestring && parse_f64_text(node->valuestring, &v)) {
        } else if (coerce && cJSON_IsBool(node)) {
            v = cJSON_IsTrue(node) ? 1.0 : 0.0;
        } else {
            return -EINVAL;
        }
        if (has_scale) {
            v = (v * scale) + offset;
        }
        if (!isfinite(v) || apply_f64_transform(&v, transform) != 0) {
            return -EINVAL;
        }
        rec.value.f64 = v;
    } else if (meta->value_type == SS_SDK_VALUE_I64) {
        int64_t v = 0;
        if (cJSON_IsNumber(node)) {
            v = (int64_t)node->valuedouble;
        } else if (coerce && cJSON_IsString(node) && node->valuestring && parse_i64_text(node->valuestring, &v)) {
        } else if (coerce && cJSON_IsBool(node)) {
            v = cJSON_IsTrue(node) ? 1 : 0;
        } else {
            return -EINVAL;
        }
        if (has_scale) {
            double dv = ((double)v * scale) + offset;
            if (!isfinite(dv)) {
                return -EINVAL;
            }
            v = (int64_t)dv;
        }
        rec.value.i64 = v;
    } else if (meta->value_type == SS_SDK_VALUE_BOOL) {
        bool v = false;
        if (cJSON_IsBool(node)) {
            v = cJSON_IsTrue(node);
        } else if (coerce && cJSON_IsNumber(node)) {
            v = ((int)node->valuedouble) != 0;
        } else if (coerce && cJSON_IsString(node) && node->valuestring && parse_bool_text(node->valuestring, &v)) {
        } else {
            return -EINVAL;
        }
        rec.value.boolean = v;
    } else if (meta->value_type == SS_SDK_VALUE_STR) {
        const char* s = NULL;
        char num_buf[64];
        char* dyn = NULL;
        if (cJSON_IsString(node) && node->valuestring) {
            s = node->valuestring;
        } else if (coerce && cJSON_IsBool(node)) {
            s = cJSON_IsTrue(node) ? "true" : "false";
        } else if (coerce && cJSON_IsNumber(node)) {
            (void)snprintf(num_buf, sizeof(num_buf), "%.17g", node->valuedouble);
            s = num_buf;
        } else if (coerce && (cJSON_IsArray(node) || cJSON_IsObject(node) || cJSON_IsNull(node))) {
            dyn = cJSON_PrintUnformatted(node);
            s = dyn;
        } else {
            return -EINVAL;
        }
        rec.value.str = s;
        bool exists = false;
        ss_sdk_status exists_rc = ss_sdk_db_record_exists(&rec, &exists);
        if (exists_rc != SS_SDK_OK) {
            if (dyn) {
                cJSON_free(dyn);
            }
            return -EIO;
        }
        if (exists) {
            if (dyn) {
                cJSON_free(dyn);
            }
            return 1;
        }
        ss_sdk_status write_rc = ss_sdk_db_write_record(&rec);
        if (dyn) {
            cJSON_free(dyn);
        }
        return write_rc == SS_SDK_OK ? 0 : -EIO;
    } else {
        return -EINVAL;
    }

    bool exists = false;
    ss_sdk_status exists_rc = ss_sdk_db_record_exists(&rec, &exists);
    if (exists_rc != SS_SDK_OK) {
        return -EIO;
    }
    if (exists) {
        return 1;
    }
    return ss_sdk_db_write_record(&rec) == SS_SDK_OK ? 0 : -EIO;
}

static void skip_spaces(const char* path, size_t* i) {
    while (path[*i] != '\0' && isspace((unsigned char)path[*i])) {
        (*i)++;
    }
}

static int apply_field_selector(const node_list* in, const char* key, node_list* out) {
    if (!in || !key || !out) {
        return -EINVAL;
    }
    for (size_t i = 0; i < in->len; i++) {
        cJSON* n = in->items[i];
        if (!cJSON_IsObject(n)) {
            continue;
        }
        cJSON* child = cJSON_GetObjectItemCaseSensitive(n, key);
        if (child) {
            int rc = node_list_push(out, child);
            if (rc != 0) {
                return rc;
            }
        }
    }
    return 0;
}

static int apply_index_selector(const node_list* in, int idx, node_list* out) {
    if (!in || !out) {
        return -EINVAL;
    }
    for (size_t i = 0; i < in->len; i++) {
        cJSON* n = in->items[i];
        if (!cJSON_IsArray(n)) {
            continue;
        }
        int arr_n = cJSON_GetArraySize(n);
        int actual = idx;
        if (idx < 0) {
            actual = arr_n + idx;
        }
        if (actual < 0 || actual >= arr_n) {
            continue;
        }
        cJSON* child = cJSON_GetArrayItem(n, actual);
        if (child) {
            int rc = node_list_push(out, child);
            if (rc != 0) {
                return rc;
            }
        }
    }
    return 0;
}

static bool json_matches_filter_value(cJSON* node, const char* expected) {
    if (!node || !expected) {
        return false;
    }
    if (cJSON_IsString(node) && node->valuestring) {
        return strcmp(node->valuestring, expected) == 0;
    }
    if (cJSON_IsNumber(node)) {
        char b[64];
        (void)snprintf(b, sizeof(b), "%.17g", node->valuedouble);
        return strcmp(b, expected) == 0;
    }
    if (cJSON_IsBool(node)) {
        return (cJSON_IsTrue(node) && strcmp(expected, "true") == 0) ||
               (!cJSON_IsTrue(node) && strcmp(expected, "false") == 0);
    }
    return false;
}

static int apply_filter_selector(const node_list* in, const char* key, const char* value, node_list* out) {
    if (!in || !key || !value || !out) {
        return -EINVAL;
    }
    for (size_t i = 0; i < in->len; i++) {
        cJSON* n = in->items[i];
        if (!cJSON_IsArray(n)) {
            continue;
        }
        int arr_n = cJSON_GetArraySize(n);
        for (int j = 0; j < arr_n; j++) {
            cJSON* item = cJSON_GetArrayItem(n, j);
            if (!cJSON_IsObject(item)) {
                continue;
            }
            cJSON* field = cJSON_GetObjectItemCaseSensitive(item, key);
            if (json_matches_filter_value(field, value)) {
                int rc = node_list_push(out, item);
                if (rc != 0) {
                    return rc;
                }
            }
        }
    }
    return 0;
}

static int apply_wildcard_selector(const node_list* in, node_list* out) {
    if (!in || !out) {
        return -EINVAL;
    }
    for (size_t i = 0; i < in->len; i++) {
        cJSON* n = in->items[i];
        if (cJSON_IsArray(n)) {
            int arr_n = cJSON_GetArraySize(n);
            for (int j = 0; j < arr_n; j++) {
                cJSON* item = cJSON_GetArrayItem(n, j);
                if (item) {
                    int rc = node_list_push(out, item);
                    if (rc != 0) {
                        return rc;
                    }
                }
            }
        } else if (cJSON_IsObject(n)) {
            cJSON* child = n->child;
            while (child) {
                int rc = node_list_push(out, child);
                if (rc != 0) {
                    return rc;
                }
                child = child->next;
            }
        }
    }
    return 0;
}

static int parse_bracket_content(const char* path, size_t* i, char* out, size_t out_sz) {
    if (!path || !i || !out || out_sz == 0 || path[*i] != '[') {
        return -EINVAL;
    }
    (*i)++;
    skip_spaces(path, i);
    size_t oi = 0;
    bool quoted = false;
    char q = '\0';
    if (path[*i] == '\'' || path[*i] == '"') {
        quoted = true;
        q = path[*i];
        (*i)++;
    }
    while (path[*i] != '\0') {
        char c = path[*i];
        if (quoted) {
            if (c == q) {
                (*i)++;
                break;
            }
        } else if (c == ']') {
            break;
        }
        if (oi + 1 < out_sz) {
            out[oi++] = c;
        }
        (*i)++;
    }
    out[oi] = '\0';
    skip_spaces(path, i);
    if (path[*i] != ']') {
        return -EINVAL;
    }
    (*i)++;
    return 0;
}

static int eval_json_path(cJSON* root, const char* path, node_list* out) {
    if (!root || !path || !out) {
        return -EINVAL;
    }

    node_list curr = {0};
    int rc = node_list_push(&curr, root);
    if (rc != 0) {
        return rc;
    }

    size_t i = 0;
    if (path[0] == '$') {
        i++;
    }

    while (path[i] != '\0') {
        if (path[i] == '.') {
            i++;
            continue;
        }

        node_list next = {0};
        if (path[i] == '[') {
            char content[256];
            rc = parse_bracket_content(path, &i, content, sizeof(content));
            if (rc != 0) {
                node_list_free(&curr);
                node_list_free(&next);
                return rc;
            }
            if (strcmp(content, "*") == 0) {
                rc = apply_wildcard_selector(&curr, &next);
            } else {
                char* eq = strchr(content, '=');
                if (eq) {
                    *eq = '\0';
                    char* key = content;
                    char* val = eq + 1;
                    while (*key != '\0' && isspace((unsigned char)*key)) {
                        key++;
                    }
                    while (*val != '\0' && isspace((unsigned char)*val)) {
                        val++;
                    }
                    rc = apply_filter_selector(&curr, key, val, &next);
                } else {
                    char* end = NULL;
                    long idx = strtol(content, &end, 10);
                    if (end != content && *end == '\0') {
                        rc = apply_index_selector(&curr, (int)idx, &next);
                    } else {
                        rc = apply_field_selector(&curr, content, &next);
                    }
                }
            }
        } else {
            char key[256];
            size_t ki = 0;
            while (path[i] != '\0' && path[i] != '.' && path[i] != '[') {
                if (ki + 1 < sizeof(key)) {
                    key[ki++] = path[i];
                }
                i++;
            }
            key[ki] = '\0';
            rc = apply_field_selector(&curr, key, &next);
        }

        node_list_free(&curr);
        if (rc != 0) {
            node_list_free(&next);
            return rc;
        }
        curr = next;
    }

    *out = curr;
    return 0;
}

static int maybe_collect_mapping_timestamps(cJSON* root, const cJSON* map, node_list* out_ts_nodes) {
    if (!root || !map || !out_ts_nodes) {
        return -EINVAL;
    }
    memset(out_ts_nodes, 0, sizeof(*out_ts_nodes));

    cJSON* ts_path = cJSON_GetObjectItemCaseSensitive((cJSON*)map, "timestamp_path");
    if (!cJSON_IsString(ts_path) || !ts_path->valuestring || ts_path->valuestring[0] == '\0') {
        return 0;
    }

    int rc = eval_json_path(root, ts_path->valuestring, out_ts_nodes);
    if (rc != 0) {
        node_list_free(out_ts_nodes);
        return 0;
    }

    return 0;
}

static bool mapping_timestamp_for_index(const node_list* ts_nodes, size_t idx, int64_t fallback_ts, int64_t* out_ts) {
    if (!out_ts) {
        return false;
    }
    *out_ts = fallback_ts;
    if (!ts_nodes || ts_nodes->len == 0) {
        return false;
    }

    if (idx >= ts_nodes->len) {
        idx = 0;
    }

    int64_t ts = 0;
    if (json_to_timestamp(ts_nodes->items[idx], &ts) && ts > 0) {
        *out_ts = ts;
        return true;
    }

    for (size_t i = 0; i < ts_nodes->len; i++) {
        if (json_to_timestamp(ts_nodes->items[i], &ts) && ts > 0) {
            *out_ts = ts;
            return true;
        }
    }
    return false;
}

static int emit_from_mapping_config(const config* worker, const emit_context* emit_ctx, const char* body,
                                    int64_t fallback_ts, int* out_records_written) {
    if (!worker || !emit_ctx || !body || !out_records_written) {
        return -EINVAL;
    }
    *out_records_written = 0;

    char* map_json = NULL;
    if (config_export_subtree_json(worker, "mapping", &map_json) != 0 || !map_json) {
        return -ENOENT;
    }

    cJSON* mappings = cJSON_Parse(map_json);
    cJSON_free(map_json);
    if (!cJSON_IsArray(mappings)) {
        cJSON_Delete(mappings);
        return -EINVAL;
    }

    cJSON* root = cJSON_Parse(body);
    if (!root) {
        cJSON_Delete(mappings);
        return -EINVAL;
    }

    int records_written = 0;
    int mapped_records = 0;
    cJSON* map = NULL;
    cJSON_ArrayForEach(map, mappings) {
        if (!cJSON_IsObject(map)) {
            continue;
        }
        cJSON* path = cJSON_GetObjectItemCaseSensitive(map, "path");
        cJSON* name = cJSON_GetObjectItemCaseSensitive(map, "canonical_name");
        if (!cJSON_IsString(path) || !path->valuestring || !cJSON_IsString(name) || !name->valuestring) {
            continue;
        }

        const ss_metric_meta* meta = metric_meta_by_name(name->valuestring);
        if (!meta) {
            continue;
        }

        cJSON* json_type = cJSON_GetObjectItemCaseSensitive(map, "json_type");
        cJSON* source_field = cJSON_GetObjectItemCaseSensitive(map, "source_field");
        cJSON* transform = cJSON_GetObjectItemCaseSensitive(map, "transform");
        cJSON* required = cJSON_GetObjectItemCaseSensitive(map, "required");
        cJSON* emit_mode = cJSON_GetObjectItemCaseSensitive(map, "emit");
        cJSON* coerce = cJSON_GetObjectItemCaseSensitive(map, "coerce");
        cJSON* scale = cJSON_GetObjectItemCaseSensitive(map, "scale");
        cJSON* offset = cJSON_GetObjectItemCaseSensitive(map, "offset");

        json_expect_type expect = JSON_EXPECT_ANY;
        if (cJSON_IsString(json_type) && json_type->valuestring && json_type->valuestring[0] != '\0') {
            expect = parse_expect_type(json_type->valuestring);
        } else {
            expect = infer_expect_from_metric_type(meta->value_type);
        }
        ss_sdk_value_transform xform = parse_transform(cJSON_IsString(transform) ? transform->valuestring : "none");
        bool req = cJSON_IsTrue(required);
        bool emit_first = cJSON_IsString(emit_mode) && strcmp(emit_mode->valuestring, "first") == 0;
        bool allow_coerce = cJSON_IsBool(coerce) && cJSON_IsTrue(coerce);
        bool has_scale = cJSON_IsNumber(scale) || cJSON_IsNumber(offset);
        double scale_v = cJSON_IsNumber(scale) ? scale->valuedouble : 1.0;
        double offset_v = cJSON_IsNumber(offset) ? offset->valuedouble : 0.0;
        const char* src_template = (cJSON_IsString(source_field) && source_field->valuestring &&
                                    source_field->valuestring[0] != '\0')
                                       ? source_field->valuestring
                                       : path->valuestring;

        node_list ts_nodes = {0};
        (void)maybe_collect_mapping_timestamps(root, map, &ts_nodes);

        node_list nodes = {0};
        int rc = eval_json_path(root, path->valuestring, &nodes);
        if (rc != 0 || nodes.len == 0) {
            if (req) {
                SS_LOG_WARN("fetch.mapping.required_missing", "required mapping path missing");
            }
            node_list_free(&nodes);
            node_list_free(&ts_nodes);
            continue;
        }

        for (size_t i = 0; i < nodes.len; i++) {
            cJSON* node = nodes.items[i];
            int64_t ts = fallback_ts;
            (void)mapping_timestamp_for_index(&ts_nodes, i, fallback_ts, &ts);
            if (!json_matches_expect(node, expect) && !allow_coerce) {
                continue;
            }
            char src_buf[512];
            if (format_source_field_indexed(src_template, i, src_buf, sizeof(src_buf)) != 0) {
                (void)snprintf(src_buf, sizeof(src_buf), "%s", src_template);
            }
            int write_rc =
                sdk_write_node(emit_ctx, meta, node, ts, src_buf, xform, allow_coerce, has_scale, scale_v, offset_v);
            if (write_rc == 0) {
                records_written++;
                mapped_records++;
                if (emit_first) {
                    break;
                }
            } else if (write_rc > 0) {
                mapped_records++;
                if (emit_first) {
                    break;
                }
            }
        }
        node_list_free(&nodes);
        node_list_free(&ts_nodes);
    }

    cJSON_Delete(root);
    cJSON_Delete(mappings);
    *out_records_written = records_written;
    return mapped_records > 0 ? 0 : -ENOENT;
}

static int resolve_config_string(const config* common, const config* worker, const char* key, char* out, size_t out_sz) {
    if (!common || !worker || !key || !out || out_sz == 0) {
        return -EINVAL;
    }
    if (strncmp(key, "common.", 7) == 0) {
        return config_get_string(common, key + 7, out, out_sz);
    }
    if (strncmp(key, "worker.", 7) == 0) {
        return config_get_string(worker, key + 7, out, out_sz);
    }
    return config_get_string(worker, key, out, out_sz);
}

static int expand_url_template(const config* common, const config* worker, const char* tmpl, char* out, size_t out_sz) {
    if (!common || !worker || !tmpl || !out || out_sz < 2) {
        return -EINVAL;
    }

    size_t oi = 0;
    for (size_t i = 0; tmpl[i] != '\0'; i++) {
        if (tmpl[i] == '$' && tmpl[i + 1] == '{') {
            i += 2;
            char key[128];
            size_t ki = 0;
            while (tmpl[i] != '\0' && tmpl[i] != '}') {
                if (ki + 1 < sizeof(key)) {
                    key[ki++] = tmpl[i];
                }
                i++;
            }
            key[ki] = '\0';
            if (tmpl[i] != '}') {
                return -EINVAL;
            }

            char val[256];
            val[0] = '\0';
            if (strncmp(key, "now.", 4) == 0) {
                time_t now = time(NULL);
                struct tm tmv;
                gmtime_r(&now, &tmv);
                if (strcmp(key, "now.year") == 0) {
                    (void)snprintf(val, sizeof(val), "%04d", tmv.tm_year + 1900);
                } else if (strcmp(key, "now.month") == 0) {
                    (void)snprintf(val, sizeof(val), "%02d", tmv.tm_mon + 1);
                } else if (strcmp(key, "now.day") == 0) {
                    (void)snprintf(val, sizeof(val), "%02d", tmv.tm_mday);
                } else if (strcmp(key, "now.epoch") == 0) {
                    (void)snprintf(val, sizeof(val), "%lld", (long long)now);
                } else {
                    return -EINVAL;
                }
            } else if (resolve_config_string(common, worker, key, val, sizeof(val)) != 0) {
                return -ENOENT;
            }

            size_t vn = strlen(val);
            if (oi + vn + 1 > out_sz) {
                return -EOVERFLOW;
            }
            memcpy(out + oi, val, vn);
            oi += vn;
            continue;
        }

        if (oi + 2 > out_sz) {
            return -EOVERFLOW;
        }
        out[oi++] = tmpl[i];
    }
    out[oi] = '\0';
    return 0;
}

int fetch_engine_run(int argc, char** argv, config* cfg, const config* common, const config* worker) {
    if (!cfg || !common || !worker) {
        return EXIT_FAILURE;
    }

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = on_term;
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGINT, &sa, NULL);

    char db_file[512];
    if (config_get_string(common, "paths.db_file", db_file, sizeof(db_file)) != 0) {
        return EXIT_FAILURE;
    }
    if (setenv("SS_SDK_DB_PATH", db_file, 1) != 0) {
        return EXIT_FAILURE;
    }

    char* cfg_json = NULL;
    if (config_export_subtree_json(cfg, "", &cfg_json) == 0 && cfg_json != NULL) {
        (void)setenv("SUNSPOTS_CONFIG", cfg_json, 1);
        cJSON_free(cfg_json);
    }

    const char* source_api = config_get_string_or(worker, "source_api", "");
    const char* source_tz = config_get_string_or(worker, "source_tz", "UTC");
    if (source_api[0] == '\0') {
        source_api = config_get_string_or(worker, "name", "fetch_worker");
    }

    ss_sdk_session_config session = {
        .source_api = source_api,
        .source_tz = source_tz,
        .default_data_kind = SS_SDK_DATA_OBSERVATION,
        .default_model_id = "",
        .default_model_run_utc = 0,
        .default_issued_at_utc = 0,
    };
    if (ss_sdk_session_begin(&session) != SS_SDK_OK) {
        return EXIT_FAILURE;
    }

    pid_t parent_pid = -1;
    if (parse_parent_pid(argc, argv, &parent_pid) != 0) {
        ss_sdk_shutdown();
        return EXIT_FAILURE;
    }

    char url_template[2048];
    if (config_get_string(worker, "request.url", url_template, sizeof(url_template)) != 0) {
        ss_sdk_shutdown();
        return EXIT_FAILURE;
    }

    const char* schedule_mode = config_get_string_or(worker, "schedule.mode", "");
    if (strcmp(schedule_mode, "interval_aligned") != 0) {
        ss_sdk_shutdown();
        return EXIT_FAILURE;
    }

    int interval_sec = clamp_positive_or_default(config_get_int_or(worker, "schedule.interval_sec", 900), 900);
    int slot_deadline_sec =
        clamp_positive_or_default(config_get_int_or(worker, "schedule.slot_deadline_sec", 120), 120);
    if (slot_deadline_sec > interval_sec) {
        slot_deadline_sec = interval_sec;
    }
    int retry_base_sec = clamp_positive_or_default(config_get_int_or(worker, "retry.base_sec", 5), 5);
    int retry_max_sec = clamp_positive_or_default(config_get_int_or(worker, "retry.max_sec", 60), 60);
    if (retry_max_sec < retry_base_sec) {
        retry_max_sec = retry_base_sec;
    }
    int retry_jitter_sec = config_get_int_or(worker, "retry.jitter_sec", 2);
    if (retry_jitter_sec < 0) {
        retry_jitter_sec = 0;
    }
    int request_timeout_sec = clamp_positive_or_default(config_get_int_or(worker, "request.timeout_sec", 15), 15);
    bool fetch_at_startup = config_get_bool_or(common, "fetch_at_startup", false);
    const char* forecast_model_id = config_get_string_or(worker, "forecast.model_id", "");
    int64_t forecast_model_run_utc = (int64_t)config_get_int_or(worker, "forecast.model_run_utc", 0);
    int64_t forecast_issued_at_utc = (int64_t)config_get_int_or(worker, "forecast.issued_at_utc", 0);

    const char* output_type = config_get_string_or(worker, "success.output_type", "");
    const char* output_target = config_get_string_or(worker, "success.output_target", "");
    const char* worker_name = config_get_string_or(worker, "name", "");
    if (strcmp(output_type, "db") != 0 || output_target[0] == '\0' || worker_name[0] == '\0') {
        ss_sdk_shutdown();
        return EXIT_FAILURE;
    }

    bool run_once = config_get_bool_or(worker, "run_once", false);

    if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) {
        ss_sdk_shutdown();
        return EXIT_FAILURE;
    }
    CURL* easy = curl_easy_init();
    if (!easy) {
        curl_global_cleanup();
        ss_sdk_shutdown();
        return EXIT_FAILURE;
    }

    curl_easy_setopt(easy, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(easy, CURLOPT_FAILONERROR, 1L);
    curl_easy_setopt(easy, CURLOPT_TIMEOUT, (long)request_timeout_sec);
    curl_easy_setopt(easy, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(easy, CURLOPT_TCP_KEEPALIVE, 1L);
    curl_easy_setopt(easy, CURLOPT_USERAGENT, "sunspots-fetcher/3.0");
    curl_easy_setopt(easy, CURLOPT_ACCEPT_ENCODING, "");
    curl_easy_setopt(easy, CURLOPT_WRITEFUNCTION, curl_write_cb);

    if (ensure_status_dirs() != 0) {
        curl_easy_cleanup(easy);
        curl_global_cleanup();
        ss_sdk_shutdown();
        return EXIT_FAILURE;
    }

    char status_path[512];
    (void)snprintf(status_path, sizeof(status_path), "runtime/status/%s.json", worker_name);

    unsigned int jitter_seed = (unsigned int)time(NULL) ^ (unsigned int)getpid();
    bool first_cycle = true;
    while (g_running) {
        char api_url[2048];
        if (expand_url_template(common, worker, url_template, api_url, sizeof(api_url)) != 0 || api_url[0] == '\0') {
            SS_LOG_ERROR("fetch.url.expand_failed", "failed to expand request url template");
            break;
        }

        time_t now = time(NULL);
        bool startup_cycle = fetch_at_startup && first_cycle;
        time_t slot_start_utc = startup_cycle ? ss_current_aligned_slot_start(now, interval_sec)
                                              : ss_next_aligned_slot_start(now, interval_sec);
        if (slot_start_utc < 0) {
            break;
        }
        if (!startup_cycle && now < slot_start_utc) {
            sleep_until_utc(slot_start_utc);
        }
        if (!g_running) {
            break;
        }

        time_t slot_deadline_utc = 0;
        time_t supervisor_deadline_utc = 0;
        if (ss_compute_slot_window(slot_start_utc, slot_deadline_sec, 0, &slot_deadline_utc, &supervisor_deadline_utc) !=
            0) {
            break;
        }
        if (startup_cycle && now > slot_deadline_utc) {
            time_t startup_deadline = now + (time_t)slot_deadline_sec;
            time_t next_slot_utc = ss_next_aligned_slot_start(now, interval_sec);
            if (next_slot_utc <= now) {
                next_slot_utc = now + (time_t)interval_sec;
            }
            if (startup_deadline > next_slot_utc) {
                startup_deadline = next_slot_utc;
            }
            slot_deadline_utc = startup_deadline;
        }
        (void)supervisor_deadline_utc;

        emit_context emit_ctx = {
            .source_api = source_api,
            .source_tz = source_tz,
            .forecast_model_id = forecast_model_id,
            .forecast_model_run_utc = forecast_model_run_utc,
            .forecast_issued_at_utc = forecast_issued_at_utc,
        };

        bool slot_ok = false;
        int records_written = 0;
        int attempt = 0;
        while (g_running) {
            now = time(NULL);
            if (now > slot_deadline_utc) {
                break;
            }

            char* body = NULL;
            size_t fetched_len = 0;
            int f_rc = fetch_text(easy, api_url, &body, &fetched_len);
            if (f_rc > 0 && fetched_len > 0) {
                int emit_written = 0;
                int emit_rc = emit_from_mapping_config(worker, &emit_ctx, body, (int64_t)slot_start_utc, &emit_written);
                free(body);
                if (emit_rc == 0) {
                    records_written = emit_written;
                    slot_ok = true;
                    (void)write_slot_status(status_path, worker_name, slot_start_utc, slot_deadline_utc, true,
                                            records_written, "");
                    break;
                }
                SS_LOG_WARN("fetch.mapping.empty", "response produced no mapped records");
            } else {
                free(body);
                SS_LOG_WARN("fetch.failed", "provider fetch failed");
            }

            now = time(NULL);
            if (now >= slot_deadline_utc) {
                break;
            }
            int delay_sec =
                ss_retry_backoff_delay_sec(attempt, retry_base_sec, retry_max_sec, retry_jitter_sec, &jitter_seed);
            if (delay_sec < 1) {
                delay_sec = 1;
            }
            time_t remaining = slot_deadline_utc - now;
            if (remaining <= 0) {
                break;
            }
            if ((time_t)delay_sec > remaining) {
                delay_sec = (int)remaining;
            }
            sleep_seconds_interruptible(delay_sec);
            attempt++;
        }

        if (!slot_ok) {
            (void)write_slot_status(status_path, worker_name, slot_start_utc, slot_deadline_utc, false, 0,
                                    "slot_deadline_missed");
        }

        (void)kill(parent_pid, SIGRTMIN);
        first_cycle = false;
        if (run_once) {
            break;
        }
    }

    curl_easy_cleanup(easy);
    curl_global_cleanup();
    ss_sdk_shutdown();
    return EXIT_SUCCESS;
}
