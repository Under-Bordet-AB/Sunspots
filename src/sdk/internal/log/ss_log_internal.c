#include "sdk/internal/log/ss_log_internal.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

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
    const char *key = "\"log_path\"";
    const char *p;
    const char *q;
    size_t n;

    if (json == NULL || out_path == NULL || out_sz == 0) {
        return -1;
    }

    /* Lightweight extraction from SUNSPOTS_CONFIG payload without JSON dependency. */
    p = strstr(json, key);
    if (p == NULL) {
        return -1;
    }

    p += strlen(key);
    while (*p != '\0' && *p != ':') {
        ++p;
    }
    if (*p != ':') {
        return -1;
    }
    ++p;

    while (*p != '\0' && isspace((unsigned char)*p)) {
        ++p;
    }

    if (*p != '"') {
        return -1;
    }
    ++p;

    q = p;
    while (*q != '\0') {
        if (*q == '"' && q > p && q[-1] != '\\') {
            break;
        }
        ++q;
    }

    if (*q != '"') {
        return -1;
    }

    n = (size_t)(q - p);
    if (n >= out_sz) {
        n = out_sz - 1;
    }

    memcpy(out_path, p, n);
    out_path[n] = '\0';
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
        out_path[0] = '\0';
        return 0;
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

    flock(fd, LOCK_UN);
    close(fd);
    return SS_SDK_OK;
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
    time_t now;
    struct tm tmv;
    struct tm *tmp_tm;
    const char *level_s;
    char *event_e;
    char *msg_e;
    char *file_e;
    char *func_e;
    char *module_e = NULL;
    char *source_api_e = NULL;
    char *line_buf = NULL;
    int needed;
    ss_sdk_status rc;

    if (event == NULL || event[0] == '\0' || message == NULL || file == NULL || func == NULL) {
        return SS_SDK_ERR_INVALID_ARG;
    }

    level_s = ss_level_to_string(level);
    if (level_s == NULL) {
        return SS_SDK_ERR_INVALID_ARG;
    }

    now = time(NULL);
    tmp_tm = gmtime(&now);
    if (tmp_tm == NULL) {
        return SS_SDK_ERR_INTERNAL;
    }
    tmv = *tmp_tm;
    if (strftime(ts, sizeof(ts), "%Y-%m-%dT%H:%M:%SZ", &tmv) == 0) {
        return SS_SDK_ERR_INTERNAL;
    }

    event_e = ss_escape_text(event);
    msg_e = ss_escape_text(message);
    file_e = ss_escape_text(file);
    func_e = ss_escape_text(func);

    if (event_e == NULL || msg_e == NULL || file_e == NULL || func_e == NULL) {
        free(event_e);
        free(msg_e);
        free(file_e);
        free(func_e);
        return SS_SDK_ERR_INTERNAL;
    }

    if (fields != NULL) {
        module_e = ss_escape_text(fields->module == NULL ? "" : fields->module);
        source_api_e = ss_escape_text(fields->source_api == NULL ? "" : fields->source_api);
        if (module_e == NULL || source_api_e == NULL) {
            free(event_e);
            free(msg_e);
            free(file_e);
            free(func_e);
            free(module_e);
            free(source_api_e);
            return SS_SDK_ERR_INTERNAL;
        }

        needed = snprintf(
            NULL,
            0,
            "%s %s %s file=%s line=%d func=%s module=%s source_api=%s metric=%d ts_utc=%lld msg=\"%s\"\n",
            ts,
            level_s,
            event_e,
            file_e,
            line,
            func_e,
            module_e,
            source_api_e,
            fields->metric,
            (long long)fields->ts_utc,
            msg_e);
    } else {
        needed = snprintf(
            NULL,
            0,
            "%s %s %s file=%s line=%d func=%s msg=\"%s\"\n",
            ts,
            level_s,
            event_e,
            file_e,
            line,
            func_e,
            msg_e);
    }

    if (needed < 0) {
        free(event_e);
        free(msg_e);
        free(file_e);
        free(func_e);
        free(module_e);
        free(source_api_e);
        return SS_SDK_ERR_INTERNAL;
    }

    line_buf = (char *)malloc((size_t)needed + 1U);
    if (line_buf == NULL) {
        free(event_e);
        free(msg_e);
        free(file_e);
        free(func_e);
        free(module_e);
        free(source_api_e);
        return SS_SDK_ERR_INTERNAL;
    }

    if (fields != NULL) {
        snprintf(
            line_buf,
            (size_t)needed + 1U,
            "%s %s %s file=%s line=%d func=%s module=%s source_api=%s metric=%d ts_utc=%lld msg=\"%s\"\n",
            ts,
            level_s,
            event_e,
            file_e,
            line,
            func_e,
            module_e,
            source_api_e,
            fields->metric,
            (long long)fields->ts_utc,
            msg_e);
    } else {
        snprintf(
            line_buf,
            (size_t)needed + 1U,
            "%s %s %s file=%s line=%d func=%s msg=\"%s\"\n",
            ts,
            level_s,
            event_e,
            file_e,
            line,
            func_e,
            msg_e);
    }

    rc = ss_log_write_line(line_buf);

    free(event_e);
    free(msg_e);
    free(file_e);
    free(func_e);
    free(module_e);
    free(source_api_e);
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
