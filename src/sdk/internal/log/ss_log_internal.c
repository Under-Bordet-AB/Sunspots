#include "sdk/internal/log/ss_log_internal.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include "libs/json/cJSON.h"

static int ss_checked_add(size_t *current_size, size_t add)
{
    /* BUGFIX(#34): explicit overflow guard in escape buffer sizing. */
    if (*current_size > SIZE_MAX - add) {
        errno = EOVERFLOW;
        return -1;
    }
    *current_size += add;
    return 0;
}

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

static const char *ss_level_to_string(ss_sdk_log_level level)
{
    switch (level) {
        case SS_SDK_LOG_DEBUG:
            return "DEBUG";
        case SS_SDK_LOG_INFO:
            return "INFO";
        case SS_SDK_LOG_WARN:
            return "WARN";
        case SS_SDK_LOG_ERROR:
            return "ERROR";
        default:
            return NULL;
    }
}

static int ss_write_all(int fd, const char *buf, size_t len)
{
    size_t off = 0;
    ssize_t nw;

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

static char *ss_escape_text(const char *s)
{
    size_t n = 0;
    size_t i;
    char *out;
    size_t j = 0;

    if (s == NULL) {
        return ss_strdup_local("");
    }

    for (i = 0; s[i] != '\0'; ++i) {
        if (s[i] == '"' || s[i] == '\\' || s[i] == '\n' || s[i] == '\t') {
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
        if (s[i] == '"') {
            out[j++] = '\\';
            out[j++] = '"';
        } else if (s[i] == '\\') {
            out[j++] = '\\';
            out[j++] = '\\';
        } else if (s[i] == '\n') {
            out[j++] = '\\';
            out[j++] = 'n';
        } else if (s[i] == '\t') {
            out[j++] = '\\';
            out[j++] = 't';
        } else {
            out[j++] = s[i];
        }
    }

    out[j] = '\0';
    return out;
}

static int ss_extract_json_log_path(const char *json, char *out_path, size_t out_sz)
{
    cJSON *root = NULL;
    cJSON *item = NULL;
    const char *path = NULL;
    size_t n = 0;

    if (json == NULL || out_path == NULL || out_sz == 0) {
        return -1;
    }

    /* BUGFIX(#39/#40): parse config with cJSON; avoid naive substring matching. */
    root = cJSON_Parse(json);
    if (root == NULL) {
        return -1;
    }

    item = cJSON_GetObjectItemCaseSensitive(root, "log_path");
    if (item == NULL) {
        out_path[0] = '\0';
        cJSON_Delete(root);
        return 0;
    }
    if (!cJSON_IsString(item) || item->valuestring == NULL) {
        cJSON_Delete(root);
        return -1;
    }
    path = item->valuestring;
    if (path[0] == '\0') {
        out_path[0] = '\0';
        cJSON_Delete(root);
        return 0;
    }
    n = strlen(path);
    if (n >= out_sz) {
        cJSON_Delete(root);
        return -1;
    }
    memcpy(out_path, path, n);
    out_path[n] = '\0';

    cJSON_Delete(root);
    return 0;
}

static int ss_get_log_path(char *out_path, size_t out_sz)
{
    const char *cfg = getenv("SUNSPOTS_CONFIG");

    if (cfg == NULL || cfg[0] == '\0') {
        out_path[0] = '\0';
        return 0;
    }

    if (ss_extract_json_log_path(cfg, out_path, out_sz) != 0) {
        /* BUGFIX(#30): malformed logging config is an explicit error, not silent no-op. */
        return -1;
    }

    return 0;
}

static ss_sdk_status ss_log_write_line(const char *line)
{
    char path[1024];
    int fd;

    if (ss_get_log_path(path, sizeof(path)) != 0) {
        return SS_SDK_ERR_INTERNAL;
    }

    if (path[0] == '\0') {
        return SS_SDK_OK;
    }

    if (ss_ensure_parent_dirs(path) != 0) {
        return SS_SDK_ERR_INTERNAL;
    }

    fd = open(path, O_WRONLY | O_CREAT | O_APPEND, 0664);
    if (fd < 0) {
        return SS_SDK_ERR_INTERNAL;
    }

    /* Serialize append writes across processes to avoid interleaved log lines. */
    if (flock(fd, LOCK_EX) != 0) {
        close(fd);
        return SS_SDK_ERR_INTERNAL;
    }

    if (ss_write_all(fd, line, strlen(line)) != 0) {
        flock(fd, LOCK_UN);
        close(fd);
        return SS_SDK_ERR_INTERNAL;
    }
    /* BUGFIX(#35): durable log write acknowledgement by default. */
    if (fsync(fd) != 0) {
        flock(fd, LOCK_UN);
        close(fd);
        return SS_SDK_ERR_INTERNAL;
    }

    flock(fd, LOCK_UN);
    close(fd);
    return SS_SDK_OK;
}

typedef struct {
    char *event;
    char *message;
    char *file;
    char *func;
    char *module;
    char *source_api;
} ss_log_escaped_fields;

static void ss_log_escaped_fields_free(ss_log_escaped_fields *escaped)
{
    if (escaped == NULL) {
        return;
    }
    free(escaped->event);
    free(escaped->message);
    free(escaped->file);
    free(escaped->func);
    free(escaped->module);
    free(escaped->source_api);
    memset(escaped, 0, sizeof(*escaped));
}

static int ss_log_format_utc_timestamp(char out_ts[32])
{
    time_t now;
    struct tm tmv;

    now = time(NULL);
    /* BUGFIX(#32): gmtime_r avoids races on shared libc time buffers. */
    if (gmtime_r(&now, &tmv) == NULL) {
        return -1;
    }
    if (strftime(out_ts, 32U, "%Y-%m-%dT%H:%M:%SZ", &tmv) == 0) {
        return -1;
    }
    return 0;
}

static int ss_log_escape_base_fields(
    ss_log_escaped_fields *escaped,
    const char *event,
    const char *message,
    const char *file,
    const char *func)
{
    escaped->event = ss_escape_text(event);
    escaped->message = ss_escape_text(message);
    escaped->file = ss_escape_text(file);
    escaped->func = ss_escape_text(func);
    if (escaped->event == NULL || escaped->message == NULL || escaped->file == NULL || escaped->func == NULL) {
        return -1;
    }
    return 0;
}

static int ss_log_escape_optional_fields(ss_log_escaped_fields *escaped, const ss_sdk_log_fields *fields)
{
    if (fields == NULL) {
        return 0;
    }

    escaped->module = ss_escape_text(fields->module == NULL ? "" : fields->module);
    escaped->source_api = ss_escape_text(fields->source_api == NULL ? "" : fields->source_api);
    if (escaped->module == NULL || escaped->source_api == NULL) {
        return -1;
    }

    return 0;
}

static int ss_log_format_line(
    char **out_line,
    const char *ts,
    const char *level_s,
    int line,
    const ss_sdk_log_fields *fields,
    const ss_log_escaped_fields *escaped)
{
    int needed;

    if (fields != NULL) {
        needed = snprintf(
            NULL,
            0,
            "%s %s %s file=%s line=%d func=%s module=%s source_api=%s metric=%d ts_utc=%lld msg=\"%s\"\n",
            ts,
            level_s,
            escaped->event,
            escaped->file,
            line,
            escaped->func,
            escaped->module,
            escaped->source_api,
            fields->metric,
            (long long)fields->ts_utc,
            escaped->message);
    } else {
        needed = snprintf(
            NULL,
            0,
            "%s %s %s file=%s line=%d func=%s msg=\"%s\"\n",
            ts,
            level_s,
            escaped->event,
            escaped->file,
            line,
            escaped->func,
            escaped->message);
    }

    if (needed < 0) {
        return -1;
    }

    *out_line = (char *)malloc((size_t)needed + 1U);
    if (*out_line == NULL) {
        return -1;
    }

    if (fields != NULL) {
        snprintf(
            *out_line,
            (size_t)needed + 1U,
            "%s %s %s file=%s line=%d func=%s module=%s source_api=%s metric=%d ts_utc=%lld msg=\"%s\"\n",
            ts,
            level_s,
            escaped->event,
            escaped->file,
            line,
            escaped->func,
            escaped->module,
            escaped->source_api,
            fields->metric,
            (long long)fields->ts_utc,
            escaped->message);
    } else {
        snprintf(
            *out_line,
            (size_t)needed + 1U,
            "%s %s %s file=%s line=%d func=%s msg=\"%s\"\n",
            ts,
            level_s,
            escaped->event,
            escaped->file,
            line,
            escaped->func,
            escaped->message);
    }

    return 0;
}

static ss_sdk_status ss_log_write_common(
    ss_sdk_log_level level,
    const char *event,
    const char *message,
    const ss_sdk_log_fields *fields,
    const char *file,
    int line,
    const char *func)
{
    char ts[32];
    const char *level_s;
    ss_log_escaped_fields escaped;
    char *line_buf = NULL;
    ss_sdk_status rc;

    if (event == NULL || event[0] == '\0' || message == NULL || file == NULL || func == NULL) {
        return SS_SDK_ERR_INVALID_ARG;
    }

    level_s = ss_level_to_string(level);
    if (level_s == NULL) {
        return SS_SDK_ERR_INVALID_ARG;
    }

    if (ss_log_format_utc_timestamp(ts) != 0) {
        return SS_SDK_ERR_INTERNAL;
    }
    memset(&escaped, 0, sizeof(escaped));
    if (ss_log_escape_base_fields(&escaped, event, message, file, func) != 0 ||
        ss_log_escape_optional_fields(&escaped, fields) != 0 ||
        ss_log_format_line(&line_buf, ts, level_s, line, fields, &escaped) != 0) {
        ss_log_escaped_fields_free(&escaped);
        free(line_buf);
        return SS_SDK_ERR_INTERNAL;
    }

    rc = ss_log_write_line(line_buf);

    ss_log_escaped_fields_free(&escaped);
    free(line_buf);

    return rc;
}

ss_sdk_status ss_sdk_internal_log_write_auto(
    ss_sdk_log_level level,
    const char *event,
    const char *message,
    const char *file,
    int line,
    const char *func)
{
    return ss_log_write_common(level, event, message, NULL, file, line, func);
}

ss_sdk_status ss_sdk_internal_log_write_fields(
    ss_sdk_log_level level,
    const char *event,
    const char *message,
    const ss_sdk_log_fields *fields,
    const char *file,
    int line,
    const char *func)
{
    return ss_log_write_common(level, event, message, fields, file, line, func);
}

void ss_sdk_internal_log_shutdown(void)
{
    /* No persistent open handles in v1. */
}
