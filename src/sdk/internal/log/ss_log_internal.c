#include "sdk/internal/log/ss_log_internal.h"
#include "sdk/internal/ss_sdk_config.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <syslog.h>
#include <time.h>
#include <unistd.h>

#include "libs/json/cJSON.h"

#ifdef SS_SDK_ENABLE_TEST_HOOKS
typedef struct {
    int fail_strdup;
    int fail_mkdir;
    int force_write_zero;
    int force_write_eintr;
    int fail_fsync;
    int fail_fsync_call;
    int fail_flock;
    int fail_gmtime;
    int fail_strftime;
    int fail_escape_base;
    int fail_escape_optional;
    int fail_format_line;
    int fail_checked_add;
    int fail_escape_alloc;
    int force_format_needed_neg;
    int force_format_alloc_null;
} ss_log_test_hooks;

static ss_log_test_hooks g_log_test_hooks;

static int ss_log_test_consume(int *slot)
{
    if (slot == NULL || *slot <= 0) {
        return 0;
    }
    *slot -= 1;
    return 1;
}

void ss_sdk_internal_log_test_reset_hooks(void)
{
    memset(&g_log_test_hooks, 0, sizeof(g_log_test_hooks));
}

void ss_sdk_internal_log_test_set_hook(ss_sdk_log_test_hook hook, int count)
{
    int safe_count = (count < 0) ? 0 : count;
    switch (hook) {
        case SS_SDK_LOG_HOOK_FAIL_STRDUP:
            g_log_test_hooks.fail_strdup = safe_count;
            break;
        case SS_SDK_LOG_HOOK_FAIL_MKDIR:
            g_log_test_hooks.fail_mkdir = safe_count;
            break;
        case SS_SDK_LOG_HOOK_FORCE_WRITE_ZERO:
            g_log_test_hooks.force_write_zero = safe_count;
            break;
        case SS_SDK_LOG_HOOK_FORCE_WRITE_EINTR:
            g_log_test_hooks.force_write_eintr = safe_count;
            break;
        case SS_SDK_LOG_HOOK_FAIL_FSYNC:
            g_log_test_hooks.fail_fsync = safe_count;
            break;
        case SS_SDK_LOG_HOOK_FAIL_FSYNC_CALL:
            g_log_test_hooks.fail_fsync_call = safe_count;
            break;
        case SS_SDK_LOG_HOOK_FAIL_FLOCK:
            g_log_test_hooks.fail_flock = safe_count;
            break;
        case SS_SDK_LOG_HOOK_FAIL_GMTIME:
            g_log_test_hooks.fail_gmtime = safe_count;
            break;
        case SS_SDK_LOG_HOOK_FAIL_STRFTIME:
            g_log_test_hooks.fail_strftime = safe_count;
            break;
        case SS_SDK_LOG_HOOK_FAIL_ESCAPE_BASE:
            g_log_test_hooks.fail_escape_base = safe_count;
            break;
        case SS_SDK_LOG_HOOK_FAIL_ESCAPE_OPTIONAL:
            g_log_test_hooks.fail_escape_optional = safe_count;
            break;
        case SS_SDK_LOG_HOOK_FAIL_FORMAT_LINE:
            g_log_test_hooks.fail_format_line = safe_count;
            break;
        case SS_SDK_LOG_HOOK_FAIL_CHECKED_ADD:
            g_log_test_hooks.fail_checked_add = safe_count;
            break;
        case SS_SDK_LOG_HOOK_FAIL_ESCAPE_ALLOC:
            g_log_test_hooks.fail_escape_alloc = safe_count;
            break;
        case SS_SDK_LOG_HOOK_FORCE_FORMAT_NEEDED_NEG:
            g_log_test_hooks.force_format_needed_neg = safe_count;
            break;
        case SS_SDK_LOG_HOOK_FORCE_FORMAT_ALLOC_NULL:
            g_log_test_hooks.force_format_alloc_null = safe_count;
            break;
        default:
            break;
    }
}
#endif

static int ss_checked_add(size_t *current_size, size_t add)
{
#ifdef SS_SDK_ENABLE_TEST_HOOKS
    if (ss_log_test_consume(&g_log_test_hooks.fail_checked_add)) {
        errno = EOVERFLOW;
        return -1;
    }
#endif

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
#ifdef SS_SDK_ENABLE_TEST_HOOKS
    int fail_alloc = 0;
#endif

    if (s == NULL) {
        return NULL;
    }
#ifdef SS_SDK_ENABLE_TEST_HOOKS
    fail_alloc = ss_log_test_consume(&g_log_test_hooks.fail_strdup);
#endif
    n = strlen(s) + 1;
    p = (char *)malloc(n);
#ifdef SS_SDK_ENABLE_TEST_HOOKS
    if (fail_alloc) {
        free(p);
        p = NULL;
        errno = ENOMEM;
    }
#endif
    if (p == NULL) {
        return NULL;
    }
    memcpy(p, s, n);
    return p;
}

static int ss_ensure_parent_dirs(const char *path)
{
    char *path_copy;
    char *slash_cursor;

    path_copy = ss_strdup_local(path);
    if (path_copy == NULL) {
        return -1;
    }

    slash_cursor = path_copy;
    while (*slash_cursor != '\0') {
        if (*slash_cursor == '/') {
            *slash_cursor = '\0';
#ifdef SS_SDK_ENABLE_TEST_HOOKS
            if (ss_log_test_consume(&g_log_test_hooks.fail_mkdir)) {
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
        ++slash_cursor;
    }

    free(path_copy);
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

static int ss_level_to_syslog_priority(ss_sdk_log_level level)
{
    switch (level) {
        case SS_SDK_LOG_DEBUG:
            return LOG_DEBUG;
        case SS_SDK_LOG_INFO:
            return LOG_INFO;
        case SS_SDK_LOG_WARN:
            return LOG_WARNING;
        case SS_SDK_LOG_ERROR:
            return LOG_ERR;
        default:
            return -1;
    }
}

static int ss_log_default_level(void)
{
    return SS_SDK_LOG_DEBUG;
}

static int ss_log_parse_level(const char *value)
{
    if (value == NULL || value[0] == '\0') {
        return ss_log_default_level();
    }

    if (strcasecmp(value, "debug") == 0) {
        return SS_SDK_LOG_DEBUG;
    }
    if (strcasecmp(value, "info") == 0) {
        return SS_SDK_LOG_INFO;
    }
    if (strcasecmp(value, "warn") == 0 || strcasecmp(value, "warning") == 0) {
        return SS_SDK_LOG_WARN;
    }
    if (strcasecmp(value, "error") == 0) {
        return SS_SDK_LOG_ERROR;
    }
    if (strcasecmp(value, "off") == 0 || strcasecmp(value, "none") == 0) {
        return SS_SDK_LOG_ERROR + 1;
    }

    return ss_log_default_level();
}

static int ss_log_min_level(void)
{
    static int sdk_env_bootstrapped = 0;
    if (!sdk_env_bootstrapped) {
        ss_sdk_config_bootstrap_env_from_blob();
        sdk_env_bootstrapped = 1;
    }

    return ss_log_parse_level(getenv(SS_SDK_ENV_LOG_LEVEL));
}

static int ss_env_bool_enabled(const char *value)
{
    if (value == NULL) {
        return 0;
    }
    if (strcmp(value, "1") == 0 ||
        strcmp(value, "true") == 0 ||
        strcmp(value, "TRUE") == 0 ||
        strcmp(value, "yes") == 0 ||
        strcmp(value, "YES") == 0 ||
        strcmp(value, "on") == 0 ||
        strcmp(value, "ON") == 0) {
        return 1;
    }
    return 0;
}

static size_t ss_log_mirror_max_bytes(void)
{
    const char *raw = getenv(SS_SDK_ENV_LOG_MIRROR_MAX_BYTES);
    unsigned long long parsed = 0;
    char *end_ptr = NULL;

    if (raw == NULL || raw[0] == '\0') {
        return (size_t)SS_SDK_LOG_MIRROR_DEFAULT_MAX_BYTES;
    }

    errno = 0;
    parsed = strtoull(raw, &end_ptr, 10);
    if (errno != 0 || end_ptr == raw || (end_ptr != NULL && *end_ptr != '\0')) {
        return (size_t)SS_SDK_LOG_MIRROR_DEFAULT_MAX_BYTES;
    }
    if (parsed == 0ULL) {
        return (size_t)SS_SDK_LOG_MIRROR_DEFAULT_MAX_BYTES;
    }
    if (parsed > (unsigned long long)SIZE_MAX) {
        return (size_t)SS_SDK_LOG_MIRROR_DEFAULT_MAX_BYTES;
    }

    return (size_t)parsed;
}

static int ss_write_all(int fd, const char *buf, size_t len)
{
    size_t write_offset = 0;
    ssize_t written_bytes;

    while (write_offset < len) {
#ifdef SS_SDK_ENABLE_TEST_HOOKS
        if (ss_log_test_consume(&g_log_test_hooks.force_write_eintr)) {
            written_bytes = -1;
            errno = EINTR;
        } else if (ss_log_test_consume(&g_log_test_hooks.force_write_zero)) {
            written_bytes = 0;
        } else {
            written_bytes = write(fd, buf + write_offset, len - write_offset);
        }
#else
        written_bytes = write(fd, buf + write_offset, len - write_offset);
#endif
        if (written_bytes < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        /* BUGFIX(#24): zero-byte write is treated as hard I/O failure. */
        if (written_bytes == 0) {
            errno = EIO;
            return -1;
        }
        write_offset += (size_t)written_bytes;
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
#ifdef SS_SDK_ENABLE_TEST_HOOKS
    if (ss_log_test_consume(&g_log_test_hooks.fail_escape_alloc)) {
        free(out);
        out = NULL;
        errno = ENOMEM;
    }
#endif
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

static const char *ss_filename_only(const char *path)
{
    const char *last_slash;
    const char *last_backslash;
    const char *base;

    if (path == NULL) {
        return "";
    }

    last_slash = strrchr(path, '/');
    last_backslash = strrchr(path, '\\');
    base = path;

    if (last_slash != NULL && last_backslash != NULL) {
        base = (last_slash > last_backslash) ? (last_slash + 1) : (last_backslash + 1);
    } else if (last_slash != NULL) {
        base = last_slash + 1;
    } else if (last_backslash != NULL) {
        base = last_backslash + 1;
    }

    if (base[0] == '\0') {
        return path;
    }
    return base;
}

#ifdef SS_SDK_ENABLE_TEST_HOOKS
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
#endif

static int ss_get_log_path(char *out_path, size_t out_sz)
{
    static int sdk_env_bootstrapped = 0;
    const char *mirror_enabled = getenv(SS_SDK_ENV_LOG_MIRROR_ENABLED);
    const char *mirror_path = getenv(SS_SDK_ENV_LOG_MIRROR_PATH);
    int mirror_is_enabled;

    if (out_path == NULL || out_sz == 0) {
        return -1;
    }
    if (!sdk_env_bootstrapped) {
        ss_sdk_config_bootstrap_env_from_blob();
        sdk_env_bootstrapped = 1;
        mirror_enabled = getenv(SS_SDK_ENV_LOG_MIRROR_ENABLED);
        mirror_path = getenv(SS_SDK_ENV_LOG_MIRROR_PATH);
    }

    mirror_is_enabled = (mirror_enabled == NULL) ? SS_SDK_LOG_MIRROR_ENABLED_DEFAULT : ss_env_bool_enabled(mirror_enabled);
    if (!mirror_is_enabled) {
        out_path[0] = '\0';
        return 0;
    }

    if (mirror_path == NULL || mirror_path[0] == '\0') {
        mirror_path = SS_SDK_LOG_MIRROR_DEFAULT_PATH;
    }

    if (strlen(mirror_path) >= out_sz) {
        return -1;
    }
    memcpy(out_path, mirror_path, strlen(mirror_path) + 1U);
    return 0;
}

static ss_sdk_status ss_log_write_mirror_line(const char *line)
{
    char path[SS_SDK_PATH_BUFFER_SIZE];
    int fd;
    struct stat st;
    size_t max_bytes;

    if (ss_get_log_path(path, sizeof(path)) != 0) {
        return SS_SDK_ERR_INTERNAL;
    }

    if (path[0] == '\0') {
        return SS_SDK_OK;
    }
    max_bytes = ss_log_mirror_max_bytes();

    if (ss_ensure_parent_dirs(path) != 0) {
        return SS_SDK_ERR_INTERNAL;
    }

    fd = open(path, O_WRONLY | O_CREAT | O_APPEND, 0664);
    if (fd < 0) {
        return SS_SDK_ERR_INTERNAL;
    }

    /* Serialize append writes across processes to avoid interleaved log lines. */
    if (
#ifdef SS_SDK_ENABLE_TEST_HOOKS
        ss_log_test_consume(&g_log_test_hooks.fail_flock) ||
#endif
        flock(fd, LOCK_EX) != 0
    ) {
        close(fd);
        return SS_SDK_ERR_INTERNAL;
    }

    /* Keep sdk.log bounded in size; once cap is reached, start from empty file. */
    if (fstat(fd, &st) != 0) {
        flock(fd, LOCK_UN);
        close(fd);
        return SS_SDK_ERR_INTERNAL;
    }
    if (st.st_size >= 0 && (size_t)st.st_size >= max_bytes) {
        if (ftruncate(fd, 0) != 0 || lseek(fd, 0, SEEK_SET) < 0) {
            flock(fd, LOCK_UN);
            close(fd);
            return SS_SDK_ERR_INTERNAL;
        }
    }

    if (ss_write_all(fd, line, strlen(line)) != 0) {
        flock(fd, LOCK_UN);
        close(fd);
        return SS_SDK_ERR_INTERNAL;
    }
    /* BUGFIX(#35): durable log write acknowledgement by default. */
#ifdef SS_SDK_ENABLE_TEST_HOOKS
    if (ss_log_test_consume(&g_log_test_hooks.fail_fsync)) {
        flock(fd, LOCK_UN);
        close(fd);
        return SS_SDK_ERR_INTERNAL;
    }
#endif
    if (
#ifdef SS_SDK_ENABLE_TEST_HOOKS
        ss_log_test_consume(&g_log_test_hooks.fail_fsync_call) ||
#endif
        fsync(fd) != 0
    ) {
        flock(fd, LOCK_UN);
        close(fd);
        return SS_SDK_ERR_INTERNAL;
    }

    flock(fd, LOCK_UN);
    close(fd);
    return SS_SDK_OK;
}

static void ss_log_write_syslog(ss_sdk_log_level level, const char *line)
{
    int priority;
    size_t len;
    size_t trimmed_len;
    char *copy;

    priority = ss_level_to_syslog_priority(level);
    if (priority < 0 || line == NULL) {
        return;
    }

    len = strlen(line);
    trimmed_len = len;
    while (trimmed_len > 0 && (line[trimmed_len - 1] == '\n' || line[trimmed_len - 1] == '\r')) {
        trimmed_len--;
    }

    copy = (char *)malloc(trimmed_len + 1U);
    if (copy == NULL) {
        return;
    }
    memcpy(copy, line, trimmed_len);
    copy[trimmed_len] = '\0';
    syslog(priority, "%s", copy);
    free(copy);
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
    struct tm *gmt_ptr;
    size_t ts_len;

    now = time(NULL);
    /* BUGFIX(#32): gmtime_r avoids races on shared libc time buffers. */
    gmt_ptr = gmtime_r(&now, &tmv);
#ifdef SS_SDK_ENABLE_TEST_HOOKS
    if (ss_log_test_consume(&g_log_test_hooks.fail_gmtime)) {
        gmt_ptr = NULL;
    }
#endif
    if (gmt_ptr == NULL) {
        return -1;
    }
    ts_len = strftime(out_ts, 32U, "%Y-%m-%dT%H:%M:%SZ", &tmv);
#ifdef SS_SDK_ENABLE_TEST_HOOKS
    if (ss_log_test_consume(&g_log_test_hooks.fail_strftime)) {
        ts_len = 0;
    }
#endif
    if (ts_len == 0) {
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
#ifdef SS_SDK_ENABLE_TEST_HOOKS
    if (ss_log_test_consume(&g_log_test_hooks.fail_escape_base)) {
        return -1;
    }
#endif

    escaped->event = ss_escape_text(event);
    escaped->message = ss_escape_text(message);
    escaped->file = ss_escape_text(ss_filename_only(file));
    escaped->func = ss_escape_text(func);
    if (escaped->event == NULL || escaped->message == NULL || escaped->file == NULL || escaped->func == NULL) {
        return -1;
    }
    return 0;
}

static int ss_log_escape_optional_fields(ss_log_escaped_fields *escaped, const ss_sdk_log_fields *fields)
{
    if (fields == NULL) {
#ifdef SS_SDK_ENABLE_TEST_HOOKS
        if (ss_log_test_consume(&g_log_test_hooks.fail_escape_optional)) {
            return -1;
        }
#endif
        return 0;
    }

    escaped->module = ss_escape_text(fields->module == NULL ? "" : fields->module);
    escaped->source_api = ss_escape_text(fields->source_api == NULL ? "" : fields->source_api);
#ifdef SS_SDK_ENABLE_TEST_HOOKS
    if (ss_log_test_consume(&g_log_test_hooks.fail_escape_optional)) {
        free(escaped->source_api);
        escaped->source_api = NULL;
    }
#endif
    if (escaped->module == NULL || escaped->source_api == NULL) {
        return -1;
    }

    return 0;
}

static int ss_log_format_line_alloc(
    char **out_line,
    const char *ts,
    const char *level_text,
    int line,
    long pid,
    const ss_sdk_log_fields *fields,
    const ss_log_escaped_fields *escaped)
{
    int format_length;
    const char *event_text = "-";

    if (escaped->event != NULL && escaped->event[0] != '\0') {
        event_text = escaped->event;
    }

#ifdef SS_SDK_ENABLE_TEST_HOOKS
    if (ss_log_test_consume(&g_log_test_hooks.fail_format_line)) {
        return -1;
    }
#endif

    if (fields != NULL) {
        format_length = snprintf(
            NULL,
            0,
            "%s %s %s pid=%ld file=%s line=%d func=%s module=%s source_api=%s metric=%d ts_utc=%lld msg=\"%s\"\n",
            ts,
            level_text,
            event_text,
            pid,
            escaped->file,
            line,
            escaped->func,
            escaped->module,
            escaped->source_api,
            fields->metric,
            (long long)fields->ts_utc,
            escaped->message);
    } else {
        format_length = snprintf(
            NULL,
            0,
            "%s %s %s pid=%ld file=%s line=%d func=%s msg=\"%s\"\n",
            ts,
            level_text,
            event_text,
            pid,
            escaped->file,
            line,
            escaped->func,
            escaped->message);
    }

#ifdef SS_SDK_ENABLE_TEST_HOOKS
    if (ss_log_test_consume(&g_log_test_hooks.force_format_needed_neg)) {
        format_length = -1;
    }
#endif
    if (format_length < 0) {
        return -1;
    }

    *out_line = (char *)malloc((size_t)format_length + 1U);
#ifdef SS_SDK_ENABLE_TEST_HOOKS
    if (ss_log_test_consume(&g_log_test_hooks.force_format_alloc_null)) {
        free(*out_line);
        *out_line = NULL;
        errno = ENOMEM;
    }
#endif
    if (*out_line == NULL) {
        return -1;
    }
    return format_length;
}

static void ss_log_format_line_write_base(
    char *out_line,
    size_t out_len,
    const char *ts,
    const char *level_text,
    const char *event_text,
    const ss_log_escaped_fields *escaped,
    int line,
    long pid)
{
    (void)snprintf(
        out_line,
        out_len,
        "%s %s %s pid=%ld file=%s line=%d func=%s msg=\"%s\"\n",
        ts,
        level_text,
        event_text,
        pid,
        escaped->file,
        line,
        escaped->func,
        escaped->message);
}

static void ss_log_format_line_write_with_fields(
    char *out_line,
    size_t out_len,
    const char *ts,
    const char *level_text,
    const char *event_text,
    const ss_sdk_log_fields *fields,
    const ss_log_escaped_fields *escaped,
    int line,
    long pid)
{
    (void)snprintf(
        out_line,
        out_len,
        "%s %s %s pid=%ld file=%s line=%d func=%s module=%s source_api=%s metric=%d ts_utc=%lld msg=\"%s\"\n",
        ts,
        level_text,
        event_text,
        pid,
        escaped->file,
        line,
        escaped->func,
        escaped->module,
        escaped->source_api,
        fields->metric,
        (long long)fields->ts_utc,
        escaped->message);
}

static int ss_log_format_line(
    char **out_line,
    const char *ts,
    const char *level_text,
    int line,
    long pid,
    const ss_sdk_log_fields *fields,
    const ss_log_escaped_fields *escaped)
{
    int format_length;
    const char *event_text = "-";

    format_length = ss_log_format_line_alloc(out_line, ts, level_text, line, pid, fields, escaped);
    if (format_length < 0) {
        return -1;
    }

    if (escaped->event != NULL && escaped->event[0] != '\0') {
        event_text = escaped->event;
    }
    if (fields != NULL) {
        ss_log_format_line_write_with_fields(
            *out_line,
            (size_t)format_length + 1U,
            ts,
            level_text,
            event_text,
            fields,
            escaped,
            line,
            pid);
    } else {
        ss_log_format_line_write_base(
            *out_line,
            (size_t)format_length + 1U,
            ts,
            level_text,
            event_text,
            escaped,
            line,
            pid);
    }

    return 0;
}

static void ss_log_write_common(
    ss_sdk_log_level level,
    const char *event,
    const char *message,
    const ss_sdk_log_fields *fields,
    const char *file,
    int line,
    const char *func)
{
    char ts[32];
    const char *level_text;
    ss_log_escaped_fields escaped;
    char *line_buf = NULL;
    long pid = (long)getpid();

    if (message == NULL || file == NULL || func == NULL) {
        return;
    }
    if ((int)level < ss_log_min_level()) {
        return;
    }

    level_text = ss_level_to_string(level);
    if (level_text == NULL) {
        return;
    }

    if (ss_log_format_utc_timestamp(ts) != 0) {
        return;
    }
    memset(&escaped, 0, sizeof(escaped));
    if (ss_log_escape_base_fields(&escaped, event, message, file, func) != 0 ||
        ss_log_escape_optional_fields(&escaped, fields) != 0 ||
        ss_log_format_line(&line_buf, ts, level_text, line, pid, fields, &escaped) != 0) {
        ss_log_escaped_fields_free(&escaped);
        free(line_buf);
        return;
    }

    ss_log_write_syslog(level, line_buf);
    (void)ss_log_write_mirror_line(line_buf);

    ss_log_escaped_fields_free(&escaped);
    free(line_buf);

    return;
}

void ss_sdk_internal_log_write_auto(
    ss_sdk_log_level level,
    const char *event,
    const char *message,
    const char *file,
    int line,
    const char *func)
{
    ss_log_write_common(level, event, message, NULL, file, line, func);
}

void ss_sdk_internal_log_write_fields(
    ss_sdk_log_level level,
    const char *event,
    const char *message,
    const ss_sdk_log_fields *fields,
    const char *file,
    int line,
    const char *func)
{
    ss_log_write_common(level, event, message, fields, file, line, func);
}

void ss_sdk_internal_log_shutdown(void)
{
    /* No persistent open handles in v1. */
}

#ifdef SS_SDK_ENABLE_TEST_HOOKS
int ss_sdk_internal_log_test_checked_add_overflow(int *out_errno)
{
    size_t cur = SIZE_MAX;
    int status_code = ss_checked_add(&cur, 1U);
    if (out_errno != NULL) {
        *out_errno = errno;
    }
    return status_code;
}

int ss_sdk_internal_log_test_ensure_parent_dirs(const char *path)
{
    return ss_ensure_parent_dirs(path);
}

const char *ss_sdk_internal_log_test_level_to_string(ss_sdk_log_level level)
{
    return ss_level_to_string(level);
}

char *ss_sdk_internal_log_test_escape_text(const char *s)
{
    return ss_escape_text(s);
}

char *ss_sdk_internal_log_test_strdup_local(const char *s)
{
    return ss_strdup_local(s);
}

int ss_sdk_internal_log_test_write_all(int fd, const char *buf, size_t len)
{
    return ss_write_all(fd, buf, len);
}

int ss_sdk_internal_log_test_extract_json_log_path(const char *json, char *out_path, size_t out_sz)
{
    return ss_extract_json_log_path(json, out_path, out_sz);
}

int ss_sdk_internal_log_test_get_log_path(char *out_path, size_t out_sz)
{
    return ss_get_log_path(out_path, out_sz);
}

int ss_sdk_internal_log_test_consume_null_slot(void)
{
    return ss_log_test_consume(NULL);
}

int ss_sdk_internal_log_test_format_utc_timestamp(char out_ts[32])
{
    return ss_log_format_utc_timestamp(out_ts);
}

void ss_sdk_internal_log_test_escaped_fields_free_null(void)
{
    ss_log_escaped_fields_free(NULL);
}
#endif
