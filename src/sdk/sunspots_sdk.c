#include "sdk/sunspots_sdk.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "core/worker_bootstrap.h"
#include "libs/json/cJSON.h"

static int sssdk_log(sssdk_runtime* rt, const char* level, const char* msg) {
    if (!rt || !msg || !level) {
        return -EINVAL;
    }
    if (!rt->logger) {
        return 0;
    }
    sunspots_log_level lvl = SUNSPOTS_LOG_INFO;
    if (strcmp(level, "warn") == 0) {
        lvl = SUNSPOTS_LOG_WARN;
    } else if (strcmp(level, "error") == 0) {
        lvl = SUNSPOTS_LOG_ERROR;
    }
    return sunspots_log_write(rt->logger, lvl, "runtime", __FILE__, __LINE__, "%s", msg);
}

static int unit_multiplier_ms(const char* unit) {
    if (!unit || unit[0] == '\0') {
        return 1000;  // default to seconds for unit-based key
    }
    if (strcmp(unit, "ms") == 0 || strcmp(unit, "millisecond") == 0 || strcmp(unit, "milliseconds") == 0) {
        return 1;
    }
    if (strcmp(unit, "s") == 0 || strcmp(unit, "sec") == 0 || strcmp(unit, "second") == 0 ||
        strcmp(unit, "seconds") == 0) {
        return 1000;
    }
    if (strcmp(unit, "m") == 0 || strcmp(unit, "min") == 0 || strcmp(unit, "minute") == 0 ||
        strcmp(unit, "minutes") == 0) {
        return 60 * 1000;
    }
    if (strcmp(unit, "h") == 0 || strcmp(unit, "hr") == 0 || strcmp(unit, "hour") == 0 ||
        strcmp(unit, "hours") == 0) {
        return 60 * 60 * 1000;
    }
    return -1;
}

static int convert_to_ms_checked(int value, int multiplier, int fallback) {
    if (value <= 0 || multiplier <= 0) {
        return fallback;
    }
    long long total = (long long) value * (long long) multiplier;
    if (total <= 0 || total > INT_MAX) {
        return fallback;
    }
    return (int) total;
}

static int resolve_record_type(const sssdk_record* rec, const char** out_type_name, sssdk_type_id* out_type_id) {
    if (!rec || !out_type_name || !out_type_id) {
        return -EINVAL;
    }

    if (rec->type_id != SSSDK_TYPE_INVALID) {
        if (!sssdk_type_is_valid(rec->type_id)) {
            return -EINVAL;
        }
        const char* name = sssdk_type_name(rec->type_id);
        if (!name) {
            return -EINVAL;
        }
        *out_type_name = name;
        *out_type_id = rec->type_id;
        return 0;
    }

    if (!rec->type) {
        return -EINVAL;
    }

    sssdk_type_id parsed = SSSDK_TYPE_INVALID;
    if (sssdk_type_parse(rec->type, &parsed) != 0) {
        return -EINVAL;
    }
    *out_type_name = rec->type;
    *out_type_id = parsed;
    return 0;
}

int sssdk_bootstrap(sssdk_runtime* rt, int argc, char** argv) {
    if (!rt || argc < 2 || !argv) {
        return -EINVAL;
    }
    memset(rt, 0, sizeof(*rt));

    int rc = worker_cfg_load_from_env(&rt->cfg, rt->version, sizeof(rt->version));
    if (rc != 0) {
        return rc;
    }

    rc = worker_cfg_get_common(rt->cfg, &rt->common);
    if (rc != 0) {
        sssdk_shutdown(rt);
        return rc;
    }
    rc = worker_cfg_get_section(rt->cfg, &rt->worker);
    if (rc != 0) {
        sssdk_shutdown(rt);
        return rc;
    }

    rc = config_get_string(rt->common, "paths.db_file", rt->db_file, sizeof(rt->db_file));
    if (rc != 0) {
        sssdk_shutdown(rt);
        return rc;
    }
    rc = config_get_string(rt->common, "paths.endpoints_dir", rt->endpoints_dir, sizeof(rt->endpoints_dir));
    if (rc != 0) {
        sssdk_shutdown(rt);
        return rc;
    }
    rc = config_get_string(rt->common, "paths.log_file", rt->log_file, sizeof(rt->log_file));
    if (rc != 0) {
        sssdk_shutdown(rt);
        return rc;
    }

    rt->heartbeat_ms = config_get_int_or(rt->common, "heartbeat.interval_ms", 1000);
    rt->parent_pid = (pid_t) atoi(argv[1]);
    rt->running = true;
    (void) sunspots_log_open(&rt->logger, rt->log_file, "worker", config_get_string_or(rt->worker, "name", ""),
                             rt->version);
    (void) sssdk_log_info(rt, "worker bootstrap complete");
    return 0;
}

int sssdk_should_run(sssdk_runtime* rt) {
    if (!rt) {
        return 0;
    }
    return rt->running ? 1 : 0;
}

int sssdk_heartbeat(sssdk_runtime* rt) {
    if (!rt || rt->parent_pid <= 1) {
        return -EINVAL;
    }
    if (kill(rt->parent_pid, SIGRTMIN) != 0) {
        return -errno;
    }
    return 0;
}

int sssdk_sleep_interval(sssdk_runtime* rt) {
    if (!rt) {
        return -EINVAL;
    }
    struct timespec ts;
    ts.tv_sec = rt->heartbeat_ms / 1000;
    ts.tv_nsec = (long) (rt->heartbeat_ms % 1000) * 1000000L;
    if (nanosleep(&ts, NULL) != 0) {
        return -errno;
    }
    return 0;
}

int sssdk_shutdown(sssdk_runtime* rt) {
    if (!rt) {
        return -EINVAL;
    }
    rt->running = false;
    sunspots_log_close(&rt->logger);
    config_destroy(&rt->cfg);
    return 0;
}

int sssdk_get_poll_interval_ms(const sssdk_runtime* rt, int default_ms) {
    if (!rt || !rt->worker) {
        return default_ms;
    }

    int hours = config_get_int_or(rt->worker, "poll_interval_hours", -1);
    if (hours > 0) {
        return convert_to_ms_checked(hours, 60 * 60 * 1000, default_ms);
    }
    int minutes = config_get_int_or(rt->worker, "poll_interval_minutes", -1);
    if (minutes > 0) {
        return convert_to_ms_checked(minutes, 60 * 1000, default_ms);
    }
    int seconds = config_get_int_or(rt->worker, "poll_interval_seconds", -1);
    if (seconds > 0) {
        return convert_to_ms_checked(seconds, 1000, default_ms);
    }

    int value = config_get_int_or(rt->worker, "poll_interval.value", -1);
    if (value > 0) {
        const char* unit = config_get_string_or(rt->worker, "poll_interval.unit", "seconds");
        int mult = unit_multiplier_ms(unit);
        if (mult > 0) {
            return convert_to_ms_checked(value, mult, default_ms);
        }
    }

    int ms = config_get_int_or(rt->worker, "poll_interval_ms", -1);
    if (ms > 0) {
        return ms;
    }

    return default_ms;
}

int sssdk_emit_record(sssdk_runtime* rt, const sssdk_record* rec) {
    if (!rt || !rec || !rec->source || !rec->payload_json) {
        return -EINVAL;
    }
    const char* type_name = NULL;
    sssdk_type_id resolved_type_id = SSSDK_TYPE_INVALID;
    int type_rc = resolve_record_type(rec, &type_name, &resolved_type_id);
    if (type_rc != 0) {
        return type_rc;
    }
    (void) resolved_type_id;
    cJSON* payload = cJSON_Parse(rec->payload_json);
    if (!payload || !cJSON_IsObject(payload)) {
        cJSON_Delete(payload);
        return -EINVAL;
    }
    cJSON_Delete(payload);

    int fd = open(rt->db_file, O_CREAT | O_APPEND | O_WRONLY | O_CLOEXEC, 0644);
    if (fd < 0) {
        return -errno;
    }
    char line[2048];
    int len = snprintf(line, sizeof(line),
                       "{\"version\":1,\"source\":\"%s\",\"type\":\"%s\",\"timestamp\":%ld,"
                       "\"payload\":%s}\n",
                       rec->source, type_name, rec->timestamp, rec->payload_json);
    if (len <= 0 || (size_t) len >= sizeof(line)) {
        close(fd);
        return -EOVERFLOW;
    }
    ssize_t written = write(fd, line, (size_t) len);
    close(fd);
    if (written != len) {
        return -EIO;
    }
    return 0;
}

int sssdk_reader_open(sssdk_runtime* rt, sssdk_reader* rd, int mode) {
    (void) mode;
    if (!rt || !rd) {
        return -EINVAL;
    }
    memset(rd, 0, sizeof(*rd));
    rd->fp = fopen(rt->db_file, "rb");
    if (!rd->fp) {
        return -errno;
    }
    return 0;
}

int sssdk_reader_next(sssdk_reader* rd, sssdk_record* out) {
    if (!rd || !rd->fp || !out) {
        return -EINVAL;
    }

    for (;;) {
        if (!fgets(rd->line_buf, sizeof(rd->line_buf), rd->fp)) {
            if (feof(rd->fp)) {
                return 0;
            }
            return -EIO;
        }

        cJSON* obj = cJSON_Parse(rd->line_buf);
        if (!obj) {
            continue;
        }

        cJSON* source = cJSON_GetObjectItemCaseSensitive(obj, "source");
        cJSON* type = cJSON_GetObjectItemCaseSensitive(obj, "type");
        cJSON* timestamp = cJSON_GetObjectItemCaseSensitive(obj, "timestamp");
        cJSON* payload = cJSON_GetObjectItemCaseSensitive(obj, "payload");

        if (!cJSON_IsString(source) || !cJSON_IsString(type) || !cJSON_IsNumber(timestamp) ||
            !cJSON_IsObject(payload)) {
            cJSON_Delete(obj);
            continue;
        }

        char* payload_txt = cJSON_PrintUnformatted(payload);
        if (!payload_txt) {
            cJSON_Delete(obj);
            continue;
        }

        long ts = (long) timestamp->valuedouble;
        int source_len = snprintf(rd->source_buf, sizeof(rd->source_buf), "%s", source->valuestring);
        int type_len = snprintf(rd->type_buf, sizeof(rd->type_buf), "%s", type->valuestring);
        int payload_len = snprintf(rd->payload_buf, sizeof(rd->payload_buf), "%s", payload_txt);
        cJSON_free(payload_txt);
        cJSON_Delete(obj);
        if (source_len <= 0 || type_len <= 0 || payload_len <= 0 ||
            (size_t) source_len >= sizeof(rd->source_buf) || (size_t) type_len >= sizeof(rd->type_buf) ||
            (size_t) payload_len >= sizeof(rd->payload_buf)) {
            continue;
        }

        rd->timestamp = ts;

        out->source = rd->source_buf;
        out->type = rd->type_buf;
        out->type_id = SSSDK_TYPE_INVALID;
        (void) sssdk_type_parse(rd->type_buf, &out->type_id);
        out->timestamp = rd->timestamp;
        out->payload_json = rd->payload_buf;
        return 1;
    }
}

int sssdk_reader_close(sssdk_reader* rd) {
    if (!rd) {
        return -EINVAL;
    }
    if (rd->fp) {
        fclose(rd->fp);
        rd->fp = NULL;
    }
    return 0;
}

int sssdk_publish_endpoint(sssdk_runtime* rt, const char* endpoint_name, const char* json_body) {
    if (!rt || !endpoint_name || !json_body) {
        return -EINVAL;
    }
    cJSON* parsed = cJSON_Parse(json_body);
    if (!parsed) {
        return -EINVAL;
    }
    cJSON_Delete(parsed);

    char tmp_path[1024];
    char out_path[1024];
    int n1 = snprintf(tmp_path, sizeof(tmp_path), "%s/.%s.tmp", rt->endpoints_dir, endpoint_name);
    int n2 = snprintf(out_path, sizeof(out_path), "%s/%s.json", rt->endpoints_dir, endpoint_name);
    if (n1 <= 0 || n2 <= 0 || (size_t) n1 >= sizeof(tmp_path) || (size_t) n2 >= sizeof(out_path)) {
        return -ENAMETOOLONG;
    }
    int fd = open(tmp_path, O_CREAT | O_TRUNC | O_WRONLY | O_CLOEXEC, 0644);
    if (fd < 0) {
        return -errno;
    }
    size_t len = strlen(json_body);
    ssize_t wr = write(fd, json_body, len);
    if (wr != (ssize_t) len) {
        close(fd);
        return -EIO;
    }
    if (fsync(fd) != 0) {
        close(fd);
        return -errno;
    }
    close(fd);
    if (rename(tmp_path, out_path) != 0) {
        return -errno;
    }
    return 0;
}

int sssdk_log_info(sssdk_runtime* rt, const char* msg) {
    return sssdk_log(rt, "info", msg);
}

int sssdk_log_warn(sssdk_runtime* rt, const char* msg) {
    return sssdk_log(rt, "warn", msg);
}

int sssdk_log_error(sssdk_runtime* rt, const char* msg) {
    return sssdk_log(rt, "error", msg);
}
