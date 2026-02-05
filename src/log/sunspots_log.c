#include "log/sunspots_log.h"

#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

enum { SUNSPOTS_LOG_LINE_MAX = 4096, SUNSPOTS_MSG_MAX = 1024 };

struct sunspots_log {
    int fd;
    char proc[64];
    char worker[64];
    char config_version[64];
};

static const char* safe(const char* s) {
    return s ? s : "";
}

static int json_escape(const char* in, char* out, size_t out_sz) {
    if (!out || out_sz == 0) {
        return -EINVAL;
    }
    size_t oi = 0;
    const char* src = safe(in);
    for (size_t i = 0; src[i] != '\0'; i++) {
        unsigned char c = (unsigned char) src[i];
        const char* esc = NULL;
        char tmp[7];
        if (c == '"' || c == '\\') {
            tmp[0] = '\\';
            tmp[1] = (char) c;
            tmp[2] = '\0';
            esc = tmp;
        } else if (c == '\n') {
            esc = "\\n";
        } else if (c == '\r') {
            esc = "\\r";
        } else if (c == '\t') {
            esc = "\\t";
        } else if (c < 0x20U) {
            (void) snprintf(tmp, sizeof(tmp), "\\u%04x", c);
            esc = tmp;
        }

        if (esc) {
            size_t n = strlen(esc);
            if (oi + n + 1 > out_sz) {
                return -EOVERFLOW;
            }
            memcpy(out + oi, esc, n);
            oi += n;
        } else {
            if (oi + 2 > out_sz) {
                return -EOVERFLOW;
            }
            out[oi++] = (char) c;
        }
    }
    out[oi] = '\0';
    return 0;
}

const char* sunspots_log_level_name(sunspots_log_level level) {
    switch (level) {
        case SUNSPOTS_LOG_TRACE:
            return "trace";
        case SUNSPOTS_LOG_DEBUG:
            return "debug";
        case SUNSPOTS_LOG_INFO:
            return "info";
        case SUNSPOTS_LOG_WARN:
            return "warn";
        case SUNSPOTS_LOG_ERROR:
            return "error";
        case SUNSPOTS_LOG_FATAL:
            return "fatal";
        default:
            return "info";
    }
}

int sunspots_log_open(sunspots_log** out, const char* path, const char* proc, const char* worker,
                      const char* config_version) {
    if (!out || !path) {
        return -EINVAL;
    }
    sunspots_log* l = calloc(1, sizeof(*l));
    if (!l) {
        return -ENOMEM;
    }
    l->fd = open(path, O_CREAT | O_APPEND | O_WRONLY | O_CLOEXEC, 0644);
    if (l->fd < 0) {
        int rc = -errno;
        free(l);
        return rc;
    }
    (void) snprintf(l->proc, sizeof(l->proc), "%s", safe(proc));
    (void) snprintf(l->worker, sizeof(l->worker), "%s", safe(worker));
    (void) snprintf(l->config_version, sizeof(l->config_version), "%s", safe(config_version));
    *out = l;
    return 0;
}

void sunspots_log_close(sunspots_log** logp) {
    if (!logp || !*logp) {
        return;
    }
    if ((*logp)->fd >= 0) {
        (void) close((*logp)->fd);
    }
    free(*logp);
    *logp = NULL;
}

int sunspots_log_vwrite(sunspots_log* log, sunspots_log_level level, const char* category, const char* file,
                        int line, const char* fmt, va_list ap) {
    if (!log || log->fd < 0 || !fmt) {
        return -EINVAL;
    }

    char msg[SUNSPOTS_MSG_MAX];
    int msg_n = vsnprintf(msg, sizeof(msg), fmt, ap);
    if (msg_n <= 0 || (size_t) msg_n >= sizeof(msg)) {
        return -EOVERFLOW;
    }

    char esc_msg[SUNSPOTS_MSG_MAX * 2];
    char esc_cat[128];
    char esc_file[256];
    int rc = json_escape(msg, esc_msg, sizeof(esc_msg));
    if (rc != 0) {
        return rc;
    }
    rc = json_escape(category, esc_cat, sizeof(esc_cat));
    if (rc != 0) {
        return rc;
    }
    rc = json_escape(file, esc_file, sizeof(esc_file));
    if (rc != 0) {
        return rc;
    }

    char ts_buf[64];
    time_t now = time(NULL);
    struct tm out_tm;
    if (!gmtime_r(&now, &out_tm)) {
        return -EIO;
    }
    if (strftime(ts_buf, sizeof(ts_buf), "%Y-%m-%dT%H:%M:%SZ", &out_tm) == 0) {
        return -EIO;
    }

    char line_buf[SUNSPOTS_LOG_LINE_MAX];
    int n = snprintf(line_buf, sizeof(line_buf),
                     "{\"ts\":\"%s\",\"level\":\"%s\",\"category\":\"%s\","
                     "\"msg\":\"%s\",\"pid\":%ld,\"proc\":\"%s\",\"worker\":\"%s\","
                     "\"file\":\"%s\",\"line\":%d,\"config_version\":\"%s\"}\n",
                     ts_buf, sunspots_log_level_name(level), esc_cat, esc_msg, (long) getpid(), safe(log->proc),
                     safe(log->worker), esc_file, line, safe(log->config_version));
    if (n <= 0 || (size_t) n >= sizeof(line_buf)) {
        return -EOVERFLOW;
    }

    ssize_t wr = write(log->fd, line_buf, (size_t) n);
    if (wr != n) {
        return -EIO;
    }
    return 0;
}

int sunspots_log_write(sunspots_log* log, sunspots_log_level level, const char* category, const char* file,
                       int line, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int rc = sunspots_log_vwrite(log, level, category, file, line, fmt, ap);
    va_end(ap);
    return rc;
}
