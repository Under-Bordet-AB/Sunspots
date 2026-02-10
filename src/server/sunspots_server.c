#include <arpa/inet.h>
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/epoll.h>
#include <sys/inotify.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include "config/config.h"
#include "core/worker_bootstrap.h"
#include "libs/json/cJSON.h"
#include "sdk/ss_sdk.h"

typedef struct endpoint_entry {
    char* name;
    char* body;
    size_t body_len;
    bool valid_json;
} endpoint_entry;

typedef struct endpoint_cache {
    endpoint_entry* items;
    size_t len;
    size_t cap;
} endpoint_cache;

static volatile sig_atomic_t g_running = 1;
static const char* k_control_command_path = "runtime/control/command.json";

static void sdk_logf(ss_sdk_log_level level, const char* event, const char* fmt, ...) {
    if (!event || !fmt) {
        return;
    }
    char msg[512];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);
    if (n <= 0) {
        return;
    }
    msg[sizeof(msg) - 1] = '\0';
    (void)ss_sdk_log_write_auto(level, event, msg, __FILE__, __LINE__, __func__);
}

static void on_term(int sig) {
    (void) sig;
    g_running = 0;
}

static int parse_parent_pid(int argc, char** argv, pid_t* out_parent_pid) {
    if (!out_parent_pid) {
        return -EINVAL;
    }
    *out_parent_pid = -1;
    if (argc < 2 || !argv) {
        return 0;
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

static int ensure_parent_dirs(const char* path) {
    if (!path || path[0] == '\0') {
        return -EINVAL;
    }
    char* tmp = strdup(path);
    if (!tmp) {
        return -ENOMEM;
    }
    for (char* p = tmp; *p != '\0'; ++p) {
        if (*p == '/') {
            *p = '\0';
            if (tmp[0] != '\0' && mkdir(tmp, 0775) != 0 && errno != EEXIST) {
                free(tmp);
                return -errno;
            }
            *p = '/';
        }
    }
    if (mkdir(tmp, 0775) != 0 && errno != EEXIST) {
        free(tmp);
        return -errno;
    }
    free(tmp);
    return 0;
}

static int ensure_parent_dirs_for_file(const char* path) {
    if (!path || path[0] == '\0') {
        return -EINVAL;
    }
    char* tmp = strdup(path);
    if (!tmp) {
        return -ENOMEM;
    }
    for (char* p = tmp; *p != '\0'; ++p) {
        if (*p == '/') {
            *p = '\0';
            if (tmp[0] != '\0' && mkdir(tmp, 0775) != 0 && errno != EEXIST) {
                free(tmp);
                return -errno;
            }
            *p = '/';
        }
    }
    free(tmp);
    return 0;
}

static void cache_free(endpoint_cache* c) {
    if (!c) {
        return;
    }
    for (size_t i = 0; i < c->len; i++) {
        free(c->items[i].name);
        free(c->items[i].body);
    }
    free(c->items);
    c->items = NULL;
    c->len = 0;
    c->cap = 0;
}

static endpoint_entry* cache_find(endpoint_cache* c, const char* name) {
    if (!c || !name) {
        return NULL;
    }
    for (size_t i = 0; i < c->len; i++) {
        if (strcmp(c->items[i].name, name) == 0) {
            return &c->items[i];
        }
    }
    return NULL;
}

static int cache_put(endpoint_cache* c, endpoint_entry* in) {
    if (!c || !in || !in->name || !in->body) {
        return -EINVAL;
    }
    endpoint_entry* existing = cache_find(c, in->name);
    if (existing) {
        free(existing->body);
        existing->body = in->body;
        existing->body_len = in->body_len;
        existing->valid_json = in->valid_json;
        free(in->name);
        in->name = NULL;
        in->body = NULL;
        return 0;
    }

    if (c->len == c->cap) {
        size_t new_cap = (c->cap == 0) ? 8 : c->cap * 2;
        endpoint_entry* p = realloc(c->items, new_cap * sizeof(*p));
        if (!p) {
            return -ENOMEM;
        }
        c->items = p;
        c->cap = new_cap;
    }
    c->items[c->len++] = *in;
    in->name = NULL;
    in->body = NULL;
    return 0;
}

static bool ends_with(const char* s, const char* suffix) {
    if (!s || !suffix) {
        return false;
    }
    size_t n = strlen(s);
    size_t m = strlen(suffix);
    if (m > n) {
        return false;
    }
    return strcmp(s + (n - m), suffix) == 0;
}

static int read_file_limited(const char* path, size_t max_bytes, char** out_body, size_t* out_len) {
    if (!path || !out_body || !out_len) {
        return -EINVAL;
    }
    struct stat st;
    if (stat(path, &st) != 0) {
        return -errno;
    }
    if (st.st_size < 0 || (size_t) st.st_size > max_bytes) {
        return -EFBIG;
    }
    FILE* f = fopen(path, "rb");
    if (!f) {
        return -errno;
    }
    size_t len = (size_t) st.st_size;
    char* body = calloc(len + 1, 1);
    if (!body) {
        (void) fclose(f);
        return -ENOMEM;
    }
    if (fread(body, 1, len, f) != len) {
        free(body);
        (void) fclose(f);
        return -EIO;
    }
    (void) fclose(f);
    body[len] = '\0';
    *out_body = body;
    *out_len = len;
    return 0;
}

static int cache_reload_dir(endpoint_cache* cache, const char* endpoints_dir, size_t max_response_bytes) {
    if (!cache || !endpoints_dir) {
        return -EINVAL;
    }

    DIR* d = opendir(endpoints_dir);
    if (!d) {
        return -errno;
    }

    endpoint_cache next = {0};
    struct dirent* ent = NULL;
    while ((ent = readdir(d)) != NULL) {
        if (!ends_with(ent->d_name, ".json")) {
            continue;
        }

        char file_path[1024];
        int n = snprintf(file_path, sizeof(file_path), "%s/%s", endpoints_dir, ent->d_name);
        if (n <= 0 || (size_t) n >= sizeof(file_path)) {
            continue;
        }

        char* body = NULL;
        size_t body_len = 0;
        int rc = read_file_limited(file_path, max_response_bytes, &body, &body_len);
        if (rc != 0) {
            continue;
        }

        size_t name_len = strlen(ent->d_name);
        if (name_len < 6) {
            free(body);
            continue;
        }
        char* name = strndup(ent->d_name, name_len - 5);
        if (!name) {
            free(body);
            closedir(d);
            cache_free(&next);
            return -ENOMEM;
        }

        cJSON* parsed = cJSON_Parse(body);
        bool valid_json = parsed != NULL;
        cJSON_Delete(parsed);

        endpoint_entry e = {.name = name, .body = body, .body_len = body_len, .valid_json = valid_json};
        if (cache_put(&next, &e) != 0) {
            free(name);
            free(body);
            closedir(d);
            cache_free(&next);
            return -ENOMEM;
        }
    }
    closedir(d);

    cache_free(cache);
    *cache = next;
    return 0;
}

static int write_all(int fd, const char* buf, size_t len) {
    size_t off = 0;
    while (off < len) {
        ssize_t n = write(fd, buf + off, len - off);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        if (n == 0) {
            return -1;
        }
        off += (size_t) n;
    }
    return 0;
}

static void build_http_date(char* out, size_t out_sz) {
    time_t now = time(NULL);
    struct tm tm_utc;
    if (!gmtime_r(&now, &tm_utc)) {
        (void) snprintf(out, out_sz, "Thu, 01 Jan 1970 00:00:00 GMT");
        return;
    }
    (void) strftime(out, out_sz, "%a, %d %b %Y %H:%M:%S GMT", &tm_utc);
}

static int send_json_response(int fd, int code, const char* reason, const char* body, size_t body_len) {
    char date_buf[64];
    build_http_date(date_buf, sizeof(date_buf));

    char hdr[384];
    int n = snprintf(hdr, sizeof(hdr),
                     "HTTP/1.1 %d %s\r\n"
                     "Content-Type: application/json\r\n"
                     "Access-Control-Allow-Origin: *\r\n"
                     "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
                     "Access-Control-Allow-Headers: Content-Type\r\n"
                     "Content-Length: %zu\r\n"
                     "Connection: close\r\n"
                     "Date: %s\r\n\r\n",
                     code, reason, body_len, date_buf);
    if (n <= 0 || (size_t) n >= sizeof(hdr)) {
        return -1;
    }
    if (write_all(fd, hdr, (size_t) n) != 0) {
        return -1;
    }
    if (body_len > 0 && write_all(fd, body, body_len) != 0) {
        return -1;
    }
    return 0;
}

static int send_response(int fd, int code, const char* reason, const char* content_type, const char* body, size_t body_len) {
    char date_buf[64];
    build_http_date(date_buf, sizeof(date_buf));

    const char* ctype = content_type ? content_type : "text/plain; charset=utf-8";
    char hdr[512];
    int n = snprintf(hdr, sizeof(hdr),
                     "HTTP/1.1 %d %s\r\n"
                     "Content-Type: %s\r\n"
                     "Access-Control-Allow-Origin: *\r\n"
                     "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
                     "Access-Control-Allow-Headers: Content-Type\r\n"
                     "Content-Length: %zu\r\n"
                     "Connection: close\r\n"
                     "Date: %s\r\n\r\n",
                     code, reason, ctype, body_len, date_buf);
    if (n <= 0 || (size_t)n >= sizeof(hdr)) {
        return -1;
    }
    if (write_all(fd, hdr, (size_t)n) != 0) {
        return -1;
    }
    if (body_len > 0 && write_all(fd, body, body_len) != 0) {
        return -1;
    }
    return 0;
}

static int send_html_response(int fd, int code, const char* reason, const char* body, size_t body_len) {
    return send_response(fd, code, reason, "text/html; charset=utf-8", body, body_len);
}

static int send_empty_response(int fd, int code, const char* reason) {
    return send_response(fd, code, reason, "text/plain; charset=utf-8", "", 0);
}

static int parse_content_length(const char* req) {
    if (!req) {
        return 0;
    }
    const char* p = req;
    while (*p) {
        if ((p == req || *(p - 1) == '\n') && strncasecmp(p, "Content-Length:", 15) == 0) {
            p += 15;
            while (*p == ' ' || *p == '\t') {
                p++;
            }
            errno = 0;
            char* end = NULL;
            long v = strtol(p, &end, 10);
            if (errno != 0 || end == p || v < 0 || v > INT_MAX) {
                return -1;
            }
            return (int)v;
        }
        p++;
    }
    return 0;
}

static int read_text_file(const char* path, size_t max_bytes, char** out, size_t* out_len) {
    return read_file_limited(path, max_bytes, out, out_len);
}

static int write_text_file_atomic(const char* path, const char* data, size_t len) {
    if (!path || !data) {
        return -EINVAL;
    }
    char tmp_path[1024];
    int n = snprintf(tmp_path, sizeof(tmp_path), "%s.tmp.%ld", path, (long)getpid());
    if (n <= 0 || (size_t)n >= sizeof(tmp_path)) {
        return -ENAMETOOLONG;
    }
    FILE* f = fopen(tmp_path, "wb");
    if (!f) {
        return -errno;
    }
    if (fwrite(data, 1, len, f) != len) {
        (void)fclose(f);
        (void)unlink(tmp_path);
        return -EIO;
    }
    if (fclose(f) != 0) {
        (void)unlink(tmp_path);
        return -EIO;
    }
    if (rename(tmp_path, path) != 0) {
        (void)unlink(tmp_path);
        return -errno;
    }
    return 0;
}

static int parse_query_int(const char* query, const char* key, int defv, int minv, int maxv) {
    if (!query || !key) {
        return defv;
    }
    size_t klen = strlen(key);
    const char* p = query;
    while (*p != '\0') {
        if (strncmp(p, key, klen) == 0 && p[klen] == '=') {
            p += klen + 1;
            errno = 0;
            char* end = NULL;
            long v = strtol(p, &end, 10);
            if (errno == 0 && end != p) {
                if (v < minv) {
                    v = minv;
                }
                if (v > maxv) {
                    v = maxv;
                }
                return (int)v;
            }
            return defv;
        }
        const char* amp = strchr(p, '&');
        if (!amp) {
            break;
        }
        p = amp + 1;
    }
    return defv;
}

static int build_logs_json(const char* log_path, int lines, char** out_json) {
    if (!log_path || !out_json) {
        return -EINVAL;
    }
    *out_json = NULL;

    char* body = NULL;
    size_t body_len = 0;
    int read_rc = read_text_file(log_path, 4 * 1024 * 1024, &body, &body_len);
    if (read_rc == -EFBIG) {
        FILE* f = fopen(log_path, "rb");
        if (f) {
            if (fseek(f, 0, SEEK_END) == 0) {
                long end = ftell(f);
                if (end > 0) {
                    const long window = 1024L * 1024L;
                    long start_off = (end > window) ? (end - window) : 0;
                    if (fseek(f, start_off, SEEK_SET) == 0) {
                        size_t cap = (size_t)(end - start_off);
                        body = calloc(cap + 1, 1);
                        if (body) {
                            size_t got = fread(body, 1, cap, f);
                            body_len = got;
                            body[body_len] = '\0';
                        }
                    }
                }
            }
            (void)fclose(f);
        }
    }
    if (!body && (read_rc != 0 || !body)) {
        cJSON* root = cJSON_CreateObject();
        cJSON_AddStringToObject(root, "path", log_path);
        cJSON_AddStringToObject(root, "tail", "");
        char* txt = cJSON_PrintUnformatted(root);
        cJSON_Delete(root);
        if (!txt) {
            return -ENOMEM;
        }
        *out_json = txt;
        return 0;
    }

    if (lines < 1) {
        lines = 200;
    }
    const char* start = body;
    int seen = 0;
    for (ssize_t i = (ssize_t)body_len - 1; i >= 0; i--) {
        if (body[i] == '\n') {
            seen++;
            if (seen >= lines) {
                start = body + i + 1;
                break;
            }
        }
    }

    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "path", log_path);
    cJSON_AddStringToObject(root, "tail", start);
    char* txt = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    free(body);
    if (!txt) {
        return -ENOMEM;
    }
    *out_json = txt;
    return 0;
}

static int write_control_command(pid_t parent_pid, const char* action, const char* worker_name) {
    if (parent_pid <= 1 || !action || action[0] == '\0') {
        return -EINVAL;
    }
    if (kill(parent_pid, 0) != 0) {
        return -ESRCH;
    }
    int rc = ensure_parent_dirs_for_file(k_control_command_path);
    if (rc != 0) {
        return rc;
    }
    cJSON* root = cJSON_CreateObject();
    if (!root) {
        return -ENOMEM;
    }
    cJSON_AddStringToObject(root, "action", action);
    cJSON_AddNumberToObject(root, "requested_at_utc", (double)time(NULL));
    if (worker_name && worker_name[0] != '\0') {
        cJSON_AddStringToObject(root, "worker", worker_name);
    }
    char* txt = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!txt) {
        return -ENOMEM;
    }
    rc = write_text_file_atomic(k_control_command_path, txt, strlen(txt));
    cJSON_free(txt);
    if (rc != 0) {
        return rc;
    }
    if (kill(parent_pid, SIGUSR1) != 0) {
        return -errno;
    }
    return 0;
}

static int build_control_status_json(pid_t parent_pid, endpoint_cache* cache, const char* db_path, const char* log_path,
                                     char** out_json) {
    if (!out_json) {
        return -EINVAL;
    }
    *out_json = NULL;

    cJSON* root = cJSON_CreateObject();
    cJSON* workers = cJSON_CreateArray();
    if (!root || !workers) {
        cJSON_Delete(root);
        cJSON_Delete(workers);
        return -ENOMEM;
    }

    cJSON_AddNumberToObject(root, "server_pid", (double)getpid());
    cJSON_AddNumberToObject(root, "daemon_pid", (double)parent_pid);
    cJSON_AddBoolToObject(root, "daemon_alive", (parent_pid > 1 && kill(parent_pid, 0) == 0));
    cJSON_AddNumberToObject(root, "endpoint_count", (double)(cache ? cache->len : 0));
    cJSON_AddStringToObject(root, "db_path", db_path ? db_path : "");
    cJSON_AddStringToObject(root, "log_path", log_path ? log_path : "");

    cJSON* endpoints = cJSON_CreateArray();
    if (cache) {
        for (size_t i = 0; i < cache->len; i++) {
            cJSON_AddItemToArray(endpoints, cJSON_CreateString(cache->items[i].name ? cache->items[i].name : ""));
        }
    }
    cJSON_AddItemToObject(root, "endpoints", endpoints);

    struct stat st;
    if (db_path && stat(db_path, &st) == 0) {
        cJSON* db = cJSON_CreateObject();
        cJSON_AddNumberToObject(db, "size_bytes", (double)st.st_size);
        cJSON_AddNumberToObject(db, "mtime_utc", (double)st.st_mtime);
        cJSON_AddItemToObject(root, "db", db);
    }

    DIR* d = opendir("runtime/status");
    if (d) {
        struct dirent* ent = NULL;
        while ((ent = readdir(d)) != NULL) {
            if (!ends_with(ent->d_name, ".json")) {
                continue;
            }
            char file_path[1024];
            int n = snprintf(file_path, sizeof(file_path), "runtime/status/%s", ent->d_name);
            if (n <= 0 || (size_t)n >= sizeof(file_path)) {
                continue;
            }
            char* body = NULL;
            size_t body_len = 0;
            if (read_text_file(file_path, 65536, &body, &body_len) != 0) {
                continue;
            }
            cJSON* parsed = cJSON_ParseWithLength(body, body_len);
            free(body);
            if (!parsed) {
                continue;
            }
            cJSON_AddItemToArray(workers, parsed);
        }
        closedir(d);
    }
    cJSON_AddItemToObject(root, "workers", workers);

    char* txt = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!txt) {
        return -ENOMEM;
    }
    *out_json = txt;
    return 0;
}

static int route_api_control_get(int client_fd, const char* path, const char* query, pid_t parent_pid,
                                 endpoint_cache* cache, const char* cfg_path, const char* db_path,
                                 const char* log_path) {
    if (strcmp(path, "/api/control/status") == 0) {
        char* txt = NULL;
        if (build_control_status_json(parent_pid, cache, db_path, log_path, &txt) != 0 || !txt) {
            const char* body = "{\"error\":\"status unavailable\"}";
            (void)send_json_response(client_fd, 500, "Internal Server Error", body, strlen(body));
            return 0;
        }
        (void)send_json_response(client_fd, 200, "OK", txt, strlen(txt));
        cJSON_free(txt);
        return 0;
    }

    if (strcmp(path, "/api/control/config") == 0) {
        char* body = NULL;
        size_t body_len = 0;
        if (read_text_file(cfg_path, 1024 * 1024, &body, &body_len) != 0 || !body) {
            const char* err = "{\"error\":\"config read failed\"}";
            (void)send_json_response(client_fd, 500, "Internal Server Error", err, strlen(err));
            return 0;
        }
        (void)send_response(client_fd, 200, "OK", "application/json", body, body_len);
        free(body);
        return 0;
    }

    if (strcmp(path, "/api/control/logs") == 0) {
        int lines = parse_query_int(query, "lines", 200, 20, 4000);
        char* txt = NULL;
        if (build_logs_json(log_path, lines, &txt) != 0 || !txt) {
            const char* body = "{\"error\":\"log read failed\"}";
            (void)send_json_response(client_fd, 500, "Internal Server Error", body, strlen(body));
            return 0;
        }
        (void)send_json_response(client_fd, 200, "OK", txt, strlen(txt));
        cJSON_free(txt);
        return 0;
    }

    if (strcmp(path, "/api/control/capabilities") == 0) {
        const char* caps =
            "{\"commands\":[\"reload_config\",\"restart_all_workers\",\"worker_start\",\"worker_stop\","
            "\"worker_restart\",\"flush_db\",\"clear_logs\",\"clear_status\",\"clean_restart\",\"stop_daemon\"]}";
        (void)send_json_response(client_fd, 200, "OK", caps, strlen(caps));
        return 0;
    }

    const char* body = "{\"error\":\"not found\"}";
    (void)send_json_response(client_fd, 404, "Not Found", body, strlen(body));
    return 0;
}

static int route_api_control_post(int client_fd, const char* path, const char* body, size_t body_len, pid_t parent_pid,
                                  const char* cfg_path) {
    if (strncmp(path, "/api/control/", 13) == 0 && kill(parent_pid, 0) != 0) {
        const char* err = "{\"error\":\"daemon not running\"}";
        (void)send_json_response(client_fd, 409, "Conflict", err, strlen(err));
        return 0;
    }

    if (strcmp(path, "/api/control/stop") == 0 || strcmp(path, "/api/control/stop_all") == 0) {
        int rc = write_control_command(parent_pid, "stop_daemon", NULL);
        if (rc != 0) {
            const char* err = "{\"error\":\"stop dispatch failed\"}";
            (void)send_json_response(client_fd, 500, "Internal Server Error", err, strlen(err));
            return 0;
        }
        const char* ok = "{\"ok\":true}";
        (void)send_json_response(client_fd, 200, "OK", ok, strlen(ok));
        return 0;
    }

    if (strcmp(path, "/api/control/restart") == 0 || strcmp(path, "/api/control/restart_all") == 0) {
        int rc = write_control_command(parent_pid, "restart_all_workers", NULL);
        if (rc != 0) {
            const char* err = "{\"error\":\"restart dispatch failed\"}";
            (void)send_json_response(client_fd, 500, "Internal Server Error", err, strlen(err));
            return 0;
        }
        const char* ok = "{\"ok\":true}";
        (void)send_json_response(client_fd, 200, "OK", ok, strlen(ok));
        return 0;
    }

    if (strcmp(path, "/api/control/reload") == 0) {
        int rc = write_control_command(parent_pid, "reload_config", NULL);
        if (rc != 0) {
            const char* err = "{\"error\":\"reload dispatch failed\"}";
            (void)send_json_response(client_fd, 500, "Internal Server Error", err, strlen(err));
            return 0;
        }
        const char* ok = "{\"ok\":true}";
        (void)send_json_response(client_fd, 200, "OK", ok, strlen(ok));
        return 0;
    }

    if (strcmp(path, "/api/control/flush_db") == 0) {
        int rc = write_control_command(parent_pid, "flush_db", NULL);
        if (rc != 0) {
            const char* err = "{\"error\":\"flush dispatch failed\"}";
            (void)send_json_response(client_fd, 500, "Internal Server Error", err, strlen(err));
            return 0;
        }
        const char* ok = "{\"ok\":true}";
        (void)send_json_response(client_fd, 200, "OK", ok, strlen(ok));
        return 0;
    }

    if (strcmp(path, "/api/control/clear_logs") == 0) {
        int rc = write_control_command(parent_pid, "clear_logs", NULL);
        if (rc != 0) {
            const char* err = "{\"error\":\"clear_logs dispatch failed\"}";
            (void)send_json_response(client_fd, 500, "Internal Server Error", err, strlen(err));
            return 0;
        }
        const char* ok = "{\"ok\":true}";
        (void)send_json_response(client_fd, 200, "OK", ok, strlen(ok));
        return 0;
    }

    if (strcmp(path, "/api/control/clear_status") == 0) {
        int rc = write_control_command(parent_pid, "clear_status", NULL);
        if (rc != 0) {
            const char* err = "{\"error\":\"clear_status dispatch failed\"}";
            (void)send_json_response(client_fd, 500, "Internal Server Error", err, strlen(err));
            return 0;
        }
        const char* ok = "{\"ok\":true}";
        (void)send_json_response(client_fd, 200, "OK", ok, strlen(ok));
        return 0;
    }

    if (strcmp(path, "/api/control/clean_restart") == 0) {
        int rc = write_control_command(parent_pid, "clean_restart", NULL);
        if (rc != 0) {
            const char* err = "{\"error\":\"clean_restart dispatch failed\"}";
            (void)send_json_response(client_fd, 500, "Internal Server Error", err, strlen(err));
            return 0;
        }
        const char* ok = "{\"ok\":true}";
        (void)send_json_response(client_fd, 200, "OK", ok, strlen(ok));
        return 0;
    }

    if (strcmp(path, "/api/control/config") == 0) {
        cJSON* parsed = cJSON_ParseWithLength(body, body_len);
        if (!parsed) {
            const char* err = "{\"error\":\"invalid json\"}";
            (void)send_json_response(client_fd, 400, "Bad Request", err, strlen(err));
            return 0;
        }
        cJSON_Delete(parsed);
        if (write_text_file_atomic(cfg_path, body, body_len) != 0) {
            const char* err = "{\"error\":\"config write failed\"}";
            (void)send_json_response(client_fd, 500, "Internal Server Error", err, strlen(err));
            return 0;
        }
        int rc = write_control_command(parent_pid, "reload_config", NULL);
        if (rc != 0) {
            const char* err = "{\"error\":\"reload dispatch failed\"}";
            (void)send_json_response(client_fd, 500, "Internal Server Error", err, strlen(err));
            return 0;
        }
        const char* ok = "{\"ok\":true}";
        (void)send_json_response(client_fd, 200, "OK", ok, strlen(ok));
        return 0;
    }

    if (strcmp(path, "/api/control/worker/start") == 0 || strcmp(path, "/api/control/worker/stop") == 0 ||
        strcmp(path, "/api/control/worker/restart") == 0) {
        cJSON* parsed = cJSON_ParseWithLength(body, body_len);
        cJSON* worker = cJSON_IsObject(parsed) ? cJSON_GetObjectItemCaseSensitive(parsed, "worker") : NULL;
        if (!cJSON_IsString(worker) || !worker->valuestring || worker->valuestring[0] == '\0') {
            cJSON_Delete(parsed);
            const char* err = "{\"error\":\"missing worker\"}";
            (void)send_json_response(client_fd, 400, "Bad Request", err, strlen(err));
            return 0;
        }
        const char* action = strcmp(path, "/api/control/worker/start") == 0
                                 ? "worker_start"
                                 : (strcmp(path, "/api/control/worker/stop") == 0 ? "worker_stop" : "worker_restart");
        int rc = write_control_command(parent_pid, action, worker->valuestring);
        cJSON_Delete(parsed);
        if (rc != 0) {
            const char* err = "{\"error\":\"control dispatch failed\"}";
            (void)send_json_response(client_fd, 500, "Internal Server Error", err, strlen(err));
            return 0;
        }
        const char* ok = "{\"ok\":true}";
        (void)send_json_response(client_fd, 200, "OK", ok, strlen(ok));
        return 0;
    }

    const char* not_found = "{\"error\":\"not found\"}";
    (void)send_json_response(client_fd, 404, "Not Found", not_found, strlen(not_found));
    return 0;
}

static int handle_client(int client_fd, endpoint_cache* cache, pid_t parent_pid, const char* cfg_path, const char* db_path,
                         const char* log_path) {
    char req[262144];
    ssize_t n = read(client_fd, req, sizeof(req) - 1);
    if (n <= 0) {
        return -1;
    }
    req[n] = '\0';

    char method[8];
    char path[256];
    char path_only[256];
    char query[256];
    if (sscanf(req, "%7s %255s", method, path) != 2) {
        const char* body = "{\"error\":\"bad request\"}";
        (void) send_json_response(client_fd, 500, "Internal Server Error", body, strlen(body));
        return 0;
    }
    path_only[0] = '\0';
    query[0] = '\0';
    const char* q = strchr(path, '?');
    if (q) {
        size_t pn = (size_t)(q - path);
        if (pn >= sizeof(path_only)) {
            const char* bad = "{\"error\":\"path too long\"}";
            (void)send_json_response(client_fd, 400, "Bad Request", bad, strlen(bad));
            return 0;
        }
        memcpy(path_only, path, pn);
        path_only[pn] = '\0';
        (void)snprintf(query, sizeof(query), "%s", q + 1);
    } else {
        (void)snprintf(path_only, sizeof(path_only), "%s", path);
    }
    char* body_ptr = strstr(req, "\r\n\r\n");
    if (!body_ptr) {
        const char* bad = "{\"error\":\"bad request\"}";
        (void)send_json_response(client_fd, 400, "Bad Request", bad, strlen(bad));
        return 0;
    }
    body_ptr += 4;
    size_t parsed_header_len = (size_t)(body_ptr - req);
    size_t payload_available = (size_t)n - parsed_header_len;
    int declared_content_len = parse_content_length(req);
    if (declared_content_len < 0) {
        const char* bad = "{\"error\":\"bad content length\"}";
        (void)send_json_response(client_fd, 400, "Bad Request", bad, strlen(bad));
        return 0;
    }
    size_t payload_len = (declared_content_len > 0) ? (size_t)declared_content_len : payload_available;
    if (payload_len > payload_available) {
        const char* bad = "{\"error\":\"incomplete request body\"}";
        (void)send_json_response(client_fd, 400, "Bad Request", bad, strlen(bad));
        return 0;
    }

    if (strcmp(method, "OPTIONS") == 0) {
        (void)send_empty_response(client_fd, 204, "No Content");
        return 0;
    }

    if (strcmp(method, "GET") == 0 && (strcmp(path_only, "/") == 0 || strcmp(path_only, "/control.html") == 0)) {
        char* html = NULL;
        size_t html_len = 0;
        if (read_text_file("control.html", 1024 * 1024, &html, &html_len) != 0 || !html) {
            const char* not_found = "<html><body><h1>control.html missing</h1></body></html>";
            (void)send_html_response(client_fd, 404, "Not Found", not_found, strlen(not_found));
            return 0;
        }
        (void)send_html_response(client_fd, 200, "OK", html, html_len);
        free(html);
        return 0;
    }

    if (strncmp(path_only, "/api/control/", 13) == 0) {
        if (strcmp(method, "GET") == 0) {
            return route_api_control_get(client_fd, path_only, query, parent_pid, cache, cfg_path, db_path, log_path);
        }
        if (strcmp(method, "POST") == 0) {
            return route_api_control_post(client_fd, path_only, body_ptr, payload_len, parent_pid, cfg_path);
        }
        const char* body = "{\"error\":\"method not allowed\"}";
        (void)send_json_response(client_fd, 405, "Method Not Allowed", body, strlen(body));
        return 0;
    }

    if (strcmp(method, "GET") != 0) {
        const char* body = "{\"error\":\"method not allowed\"}";
        (void) send_json_response(client_fd, 405, "Method Not Allowed", body, strlen(body));
        return 0;
    }
    if (strncmp(path_only, "/api/", 5) != 0) {
        const char* body = "{\"error\":\"not found\"}";
        (void) send_json_response(client_fd, 404, "Not Found", body, strlen(body));
        return 0;
    }

    const char* endpoint = path_only + 5;
    if (strstr(endpoint, "/") || strstr(endpoint, "..") || endpoint[0] == '\0') {
        const char* body = "{\"error\":\"not found\"}";
        (void) send_json_response(client_fd, 404, "Not Found", body, strlen(body));
        return 0;
    }

    endpoint_entry* e = cache_find(cache, endpoint);
    if (!e) {
        const char* body = "{\"error\":\"endpoint missing\"}";
        (void) send_json_response(client_fd, 404, "Not Found", body, strlen(body));
        return 0;
    }
    if (!e->valid_json) {
        const char* body = "{\"error\":\"invalid endpoint json\"}";
        (void) send_json_response(client_fd, 500, "Internal Server Error", body, strlen(body));
        return 0;
    }
    (void) send_json_response(client_fd, 200, "OK", e->body, e->body_len);
    return 0;
}

int main(int argc, char** argv) {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = on_term;
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGINT, &sa, NULL);

    pid_t parent_pid = -1;
    if (parse_parent_pid(argc, argv, &parent_pid) != 0) {
        return EXIT_FAILURE;
    }

    config* cfg = NULL;
    char version[64];
    if (worker_cfg_load_from_env(&cfg, version, sizeof(version)) != 0) {
        return EXIT_FAILURE;
    }

    const config* common = NULL;
    const config* worker = NULL;
    if (worker_cfg_get_common(cfg, &common) != 0 || worker_cfg_get_section(cfg, &worker) != 0) {
        config_destroy(&cfg);
        return EXIT_FAILURE;
    }

    char endpoints_dir[512];
    char cfg_path[512];
    char db_path[512];
    char log_path[512];
    char host[64];
    if (config_get_string(common, "paths.endpoints_dir", endpoints_dir, sizeof(endpoints_dir)) != 0) {
        config_destroy(&cfg);
        return EXIT_FAILURE;
    }
    if (config_get_string(common, "paths.db_file", db_path, sizeof(db_path)) != 0) {
        (void)snprintf(db_path, sizeof(db_path), ".db/database.csv");
    }
    if (config_get_string(common, "paths.log_file", log_path, sizeof(log_path)) != 0) {
        (void)snprintf(log_path, sizeof(log_path), "logs/sunspots.log");
    }
    if (config_get_string(common, "paths.config_file", cfg_path, sizeof(cfg_path)) != 0) {
        const char* env_cfg = getenv("SUNSPOTS_MASTER_CONFIG");
        if (env_cfg && env_cfg[0] != '\0' && strlen(env_cfg) < sizeof(cfg_path)) {
            (void)snprintf(cfg_path, sizeof(cfg_path), "%s", env_cfg);
        } else {
            (void)snprintf(cfg_path, sizeof(cfg_path), "config/sunspots.json");
        }
    }
    if (config_get_string(worker, "listen_host", host, sizeof(host)) != 0) {
        (void) snprintf(host, sizeof(host), "127.0.0.1");
    }
    int port = config_get_int_or(worker, "listen_port", 8080);
    int max_response_bytes = config_get_int_or(worker, "max_response_bytes", 1048576);
    if (max_response_bytes <= 0) {
        max_response_bytes = 1048576;
    }
    int fallback_scan_sec = config_get_int_or(worker, "watch.fallback_scan_sec", 60);
    if (fallback_scan_sec <= 0) {
        fallback_scan_sec = 60;
    }
    int read_timeout_ms = config_get_int_or(worker, "read_timeout_ms", 2000);
    if (read_timeout_ms <= 0) {
        read_timeout_ms = 2000;
    }
    int heartbeat_interval_ms = config_get_int_or(common, "heartbeat.interval_ms", 1000);
    if (heartbeat_interval_ms <= 0) {
        heartbeat_interval_ms = 1000;
    }
    char* cfg_json = NULL;
    if (config_export_subtree_json(cfg, "", &cfg_json) == 0 && cfg_json != NULL) {
        (void)setenv("SUNSPOTS_CONFIG", cfg_json, 1);
        cJSON_free(cfg_json);
    }

    if (ensure_parent_dirs(endpoints_dir) != 0) {
        config_destroy(&cfg);
        return EXIT_FAILURE;
    }

    int listen_fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (listen_fd < 0) {
        sdk_logf(SS_SDK_LOG_ERROR, "server.socket.create_failed", "failed to create listen socket");
        config_destroy(&cfg);
        return EXIT_FAILURE;
    }
    int one = 1;
    (void) setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((unsigned short) port);
    if (inet_pton(AF_INET, host, &addr.sin_addr) != 1) {
        close(listen_fd);
        config_destroy(&cfg);
        return EXIT_FAILURE;
    }
    if (bind(listen_fd, (struct sockaddr*) &addr, sizeof(addr)) != 0 || listen(listen_fd, 128) != 0) {
        close(listen_fd);
        config_destroy(&cfg);
        return EXIT_FAILURE;
    }
    sdk_logf(SS_SDK_LOG_INFO, "server.start", "server listening on %s:%d", host, port);

    int epoll_fd = epoll_create1(EPOLL_CLOEXEC);
    if (epoll_fd < 0) {
        close(listen_fd);
        config_destroy(&cfg);
        return EXIT_FAILURE;
    }

    int inotify_fd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
    if (inotify_fd < 0) {
        close(epoll_fd);
        close(listen_fd);
        config_destroy(&cfg);
        return EXIT_FAILURE;
    }
    int wd = inotify_add_watch(inotify_fd, endpoints_dir, IN_CLOSE_WRITE | IN_MOVED_TO | IN_CREATE | IN_DELETE);
    if (wd < 0) {
        close(inotify_fd);
        close(epoll_fd);
        close(listen_fd);
        config_destroy(&cfg);
        return EXIT_FAILURE;
    }

    struct epoll_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.events = EPOLLIN;
    ev.data.fd = listen_fd;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, listen_fd, &ev) != 0) {
        close(inotify_fd);
        close(epoll_fd);
        close(listen_fd);
        config_destroy(&cfg);
        return EXIT_FAILURE;
    }
    ev.data.fd = inotify_fd;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, inotify_fd, &ev) != 0) {
        close(inotify_fd);
        close(epoll_fd);
        close(listen_fd);
        config_destroy(&cfg);
        return EXIT_FAILURE;
    }

    endpoint_cache cache = {0};
    (void) cache_reload_dir(&cache, endpoints_dir, (size_t) max_response_bytes);
    sdk_logf(SS_SDK_LOG_INFO, "server.cache.initial_loaded", "initial endpoint cache loaded");

    struct epoll_event events[32];
    time_t last_heartbeat_utc = 0;
    while (g_running) {
        int wait_ms = fallback_scan_sec * 1000;
        if (parent_pid > 1 && heartbeat_interval_ms < wait_ms) {
            wait_ms = heartbeat_interval_ms;
        }
        int n = epoll_wait(epoll_fd, events, 32, wait_ms);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            break;
        }
        if (n == 0) {
            (void) cache_reload_dir(&cache, endpoints_dir, (size_t) max_response_bytes);
            continue;
        }

        for (int i = 0; i < n; i++) {
            int fd = events[i].data.fd;
            if (fd == inotify_fd) {
                char buf[4096];
                while (read(inotify_fd, buf, sizeof(buf)) > 0) {}
                (void) cache_reload_dir(&cache, endpoints_dir, (size_t) max_response_bytes);
                sdk_logf(SS_SDK_LOG_DEBUG, "server.cache.reloaded", "cache reloaded from inotify event");
                continue;
            }

            if (fd == listen_fd) {
                for (;;) {
                    int client_fd = accept4(listen_fd, NULL, NULL, SOCK_CLOEXEC);
                    if (client_fd < 0) {
                        if (errno == EAGAIN || errno == EWOULDBLOCK) {
                            break;
                        }
                        break;
                    }
                    struct timeval tv;
                    tv.tv_sec = read_timeout_ms / 1000;
                    tv.tv_usec = (suseconds_t) ((suseconds_t) (read_timeout_ms % 1000) * 1000);
                    (void) setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
                    (void) setsockopt(client_fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
                    (void) handle_client(client_fd, &cache, parent_pid, cfg_path, db_path, log_path);
                    close(client_fd);
                }
            }
        }

        if (parent_pid > 1) {
            time_t now_utc = time(NULL);
            int hb_sec = (heartbeat_interval_ms + 999) / 1000;
            if (hb_sec < 1) {
                hb_sec = 1;
            }
            if (last_heartbeat_utc == 0 || (now_utc - last_heartbeat_utc) >= hb_sec) {
                (void)kill(parent_pid, SIGRTMIN);
                last_heartbeat_utc = now_utc;
            }
        }
    }

    sdk_logf(SS_SDK_LOG_INFO, "server.shutdown", "server shutdown");
    cache_free(&cache);
    close(inotify_fd);
    close(epoll_fd);
    close(listen_fd);
    config_destroy(&cfg);
    ss_sdk_shutdown();
    return EXIT_SUCCESS;
}
