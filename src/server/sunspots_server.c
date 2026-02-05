#include <arpa/inet.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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
#include "log/sunspots_log.h"

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

static void on_term(int sig) {
    (void) sig;
    g_running = 0;
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
        fclose(f);
        return -ENOMEM;
    }
    if (fread(body, 1, len, f) != len) {
        free(body);
        fclose(f);
        return -EIO;
    }
    fclose(f);
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
        snprintf(out, out_sz, "Thu, 01 Jan 1970 00:00:00 GMT");
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

static int handle_client(int client_fd, endpoint_cache* cache) {
    char req[1024];
    ssize_t n = read(client_fd, req, sizeof(req) - 1);
    if (n <= 0) {
        return -1;
    }
    req[n] = '\0';

    char method[8];
    char path[256];
    if (sscanf(req, "%7s %255s", method, path) != 2) {
        const char* body = "{\"error\":\"bad request\"}";
        (void) send_json_response(client_fd, 500, "Internal Server Error", body, strlen(body));
        return 0;
    }
    if (strcmp(method, "GET") != 0) {
        const char* body = "{\"error\":\"method not allowed\"}";
        (void) send_json_response(client_fd, 405, "Method Not Allowed", body, strlen(body));
        return 0;
    }
    if (strncmp(path, "/api/", 5) != 0) {
        const char* body = "{\"error\":\"not found\"}";
        (void) send_json_response(client_fd, 404, "Not Found", body, strlen(body));
        return 0;
    }

    const char* endpoint = path + 5;
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

int main(void) {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = on_term;
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGINT, &sa, NULL);

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
    char host[64];
    if (config_get_string(common, "paths.endpoints_dir", endpoints_dir, sizeof(endpoints_dir)) != 0) {
        config_destroy(&cfg);
        return EXIT_FAILURE;
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
    char log_file[512];
    sunspots_log* log = NULL;
    if (config_get_string(common, "paths.log_file", log_file, sizeof(log_file)) == 0) {
        (void) sunspots_log_open(&log, log_file, "server", "server", version);
    }

    int listen_fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (listen_fd < 0) {
        (void) SUNSPOTS_LOG_ERROR(log, "http", "failed to create listen socket");
        config_destroy(&cfg);
        sunspots_log_close(&log);
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
        sunspots_log_close(&log);
        return EXIT_FAILURE;
    }
    if (bind(listen_fd, (struct sockaddr*) &addr, sizeof(addr)) != 0 || listen(listen_fd, 128) != 0) {
        close(listen_fd);
        config_destroy(&cfg);
        sunspots_log_close(&log);
        return EXIT_FAILURE;
    }
    (void) SUNSPOTS_LOG_INFO(log, "http", "server listening on %s:%d", host, port);

    int epoll_fd = epoll_create1(EPOLL_CLOEXEC);
    if (epoll_fd < 0) {
        close(listen_fd);
        config_destroy(&cfg);
        sunspots_log_close(&log);
        return EXIT_FAILURE;
    }

    int inotify_fd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
    if (inotify_fd < 0) {
        close(epoll_fd);
        close(listen_fd);
        config_destroy(&cfg);
        sunspots_log_close(&log);
        return EXIT_FAILURE;
    }
    int wd = inotify_add_watch(inotify_fd, endpoints_dir, IN_CLOSE_WRITE | IN_MOVED_TO | IN_CREATE | IN_DELETE);
    if (wd < 0) {
        close(inotify_fd);
        close(epoll_fd);
        close(listen_fd);
        config_destroy(&cfg);
        sunspots_log_close(&log);
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
        sunspots_log_close(&log);
        return EXIT_FAILURE;
    }
    ev.data.fd = inotify_fd;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, inotify_fd, &ev) != 0) {
        close(inotify_fd);
        close(epoll_fd);
        close(listen_fd);
        config_destroy(&cfg);
        sunspots_log_close(&log);
        return EXIT_FAILURE;
    }

    endpoint_cache cache = {0};
    (void) cache_reload_dir(&cache, endpoints_dir, (size_t) max_response_bytes);
    (void) SUNSPOTS_LOG_INFO(log, "cache", "initial endpoint cache loaded");

    struct epoll_event events[32];
    while (g_running) {
        int n = epoll_wait(epoll_fd, events, 32, fallback_scan_sec * 1000);
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
                while (read(inotify_fd, buf, sizeof(buf)) > 0) {
                }
                (void) cache_reload_dir(&cache, endpoints_dir, (size_t) max_response_bytes);
                (void) SUNSPOTS_LOG_DEBUG(log, "cache", "cache reloaded from inotify event");
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
                    tv.tv_usec = (read_timeout_ms % 1000) * 1000;
                    (void) setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
                    (void) setsockopt(client_fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
                    (void) handle_client(client_fd, &cache);
                    close(client_fd);
                }
            }
        }
    }

    (void) SUNSPOTS_LOG_INFO(log, "http", "server shutdown");
    cache_free(&cache);
    close(inotify_fd);
    close(epoll_fd);
    close(listen_fd);
    config_destroy(&cfg);
    sunspots_log_close(&log);
    return EXIT_SUCCESS;
}
