#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/inotify.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "config/config.h"
#include "core/runtime_supervision.h"
#include "libs/json/cJSON.h"
#include "sdk/ss_sdk.h"

typedef struct worker_state {
    char* name;
    char* binary;
    pid_t pid;
    time_t last_heartbeat;
    time_t last_deadline_restart_slot_utc;
    bool restart_pending;
    time_t restart_not_before_utc;
    int restart_attempt;
    bool deadline_supervision;
    int interval_sec;
    int slot_deadline_sec;
    int grace_sec;
    time_t deadline_supervision_resume_utc;
} worker_state;

enum {
    RESTART_BACKOFF_BASE_SEC = 2,
    RESTART_BACKOFF_MAX_SEC = 300,
};

static const char* CONTROL_COMMAND_PATH = "runtime/control/command.json";
static const char* DAEMON_LOCK_PATH = "runtime/control/sunspotsd.lock";
static int g_daemon_lock_fd = -1;

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

static char* dupstr(const char* s) {
    if (!s) {
        return NULL;
    }
    size_t n = strlen(s) + 1;
    char* p = malloc(n);
    if (!p) {
        return NULL;
    }
    memcpy(p, s, n);
    return p;
}

static void worker_free(worker_state* w) {
    if (!w) {
        return;
    }
    free(w->name);
    free(w->binary);
    memset(w, 0, sizeof(*w));
}

static void workers_free(worker_state** workers, size_t* count) {
    if (!workers || !*workers) {
        if (count) {
            *count = 0;
        }
        return;
    }
    if (count) {
        for (size_t i = 0; i < *count; i++) {
            worker_free(&(*workers)[i]);
        }
    }
    free(*workers);
    *workers = NULL;
    if (count) {
        *count = 0;
    }
}

static int ensure_runtime_dirs(void) {
    if (mkdir("runtime", 0755) != 0 && errno != EEXIST) {
        return -errno;
    }
    if (mkdir("runtime/status", 0755) != 0 && errno != EEXIST) {
        return -errno;
    }
    if (mkdir("runtime/control", 0755) != 0 && errno != EEXIST) {
        return -errno;
    }
    return 0;
}

static int acquire_daemon_lock(void) {
    int fd = open(DAEMON_LOCK_PATH, O_RDWR | O_CREAT, 0644);
    if (fd < 0) {
        return -errno;
    }
    struct flock fl;
    memset(&fl, 0, sizeof(fl));
    fl.l_type = F_WRLCK;
    fl.l_whence = SEEK_SET;
    if (fcntl(fd, F_SETLK, &fl) != 0) {
        int rc = -errno;
        close(fd);
        return rc;
    }
    char buf[32];
    int n = snprintf(buf, sizeof(buf), "%ld\n", (long)getpid());
    if (n > 0) {
        (void)ftruncate(fd, 0);
        (void)lseek(fd, 0, SEEK_SET);
        (void)write(fd, buf, (size_t)n);
    }
    g_daemon_lock_fd = fd;
    return 0;
}

static void release_daemon_lock(void) {
    if (g_daemon_lock_fd >= 0) {
        close(g_daemon_lock_fd);
        g_daemon_lock_fd = -1;
    }
}

static bool is_target_worker_name(const char* name) {
    if (!name) {
        return false;
    }
    return strcmp(name, "fetch_worker") == 0 || strcmp(name, "sunspots_server") == 0;
}

static void terminate_pid_graceful(pid_t pid) {
    if (pid <= 1 || pid == getpid()) {
        return;
    }
    if (kill(pid, SIGTERM) != 0 && errno != ESRCH) {
        return;
    }
    for (int i = 0; i < 20; i++) {
        if (kill(pid, 0) != 0) {
            return;
        }
        struct timespec ts = {.tv_sec = 0, .tv_nsec = 100 * 1000 * 1000};
        (void)nanosleep(&ts, NULL);
    }
    (void)kill(pid, SIGKILL);
}

/* Cleanup stale worker/server processes not owned by a live daemon (PPid=1). */
static void cleanup_orphan_runtime_processes(void) {
    DIR* d = opendir("/proc");
    if (!d) {
        return;
    }
    struct dirent* ent = NULL;
    while ((ent = readdir(d)) != NULL) {
        if (!ent->d_name || !isdigit((unsigned char)ent->d_name[0])) {
            continue;
        }
        char path[128];
        int n = snprintf(path, sizeof(path), "/proc/%s/status", ent->d_name);
        if (n <= 0 || (size_t)n >= sizeof(path)) {
            continue;
        }
        FILE* f = fopen(path, "r");
        if (!f) {
            continue;
        }
        char name[64] = {0};
        long ppid = -1;
        char state = '\0';
        char line[256];
        while (fgets(line, sizeof(line), f) != NULL) {
            if (strncmp(line, "Name:", 5) == 0) {
                (void)sscanf(line + 5, "%63s", name);
            } else if (strncmp(line, "PPid:", 5) == 0) {
                (void)sscanf(line + 5, "%ld", &ppid);
            } else if (strncmp(line, "State:", 6) == 0) {
                (void)sscanf(line + 6, " %c", &state);
            }
        }
        fclose(f);
        if (!is_target_worker_name(name)) {
            continue;
        }
        if (state == 'Z') {
            continue;
        }
        if (ppid != 1) {
            continue;
        }
        pid_t pid = (pid_t)atoi(ent->d_name);
        terminate_pid_graceful(pid);
    }
    closedir(d);
}

static int ensure_parent_dirs_for_file(const char* path) {
    if (!path || path[0] == '\0') {
        return -EINVAL;
    }
    char* tmp = dupstr(path);
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

static int flush_db_file(const config* common) {
    if (!common) {
        return -EINVAL;
    }
    char db_path[512];
    if (config_get_string(common, "paths.db_file", db_path, sizeof(db_path)) != 0) {
        return -ENOENT;
    }
    int rc = ensure_parent_dirs_for_file(db_path);
    if (rc != 0) {
        return rc;
    }
    FILE* f = fopen(db_path, "wb");
    if (!f) {
        return -errno;
    }
    if (fclose(f) != 0) {
        return -EIO;
    }
    return 0;
}

static int clear_log_file(const config* common) {
    if (!common) {
        return -EINVAL;
    }
    char log_path[512];
    if (config_get_string(common, "paths.log_file", log_path, sizeof(log_path)) != 0) {
        return -ENOENT;
    }
    int rc = ensure_parent_dirs_for_file(log_path);
    if (rc != 0) {
        return rc;
    }
    FILE* f = fopen(log_path, "wb");
    if (!f) {
        return -errno;
    }
    if (fclose(f) != 0) {
        return -EIO;
    }
    return 0;
}

static int read_text_file_limited(const char* path, size_t max_bytes, char** out_body, size_t* out_len) {
    if (!path || !out_body || !out_len) {
        return -EINVAL;
    }
    *out_body = NULL;
    *out_len = 0;

    struct stat st;
    if (stat(path, &st) != 0) {
        return -errno;
    }
    if (st.st_size < 0 || (size_t)st.st_size > max_bytes) {
        return -EFBIG;
    }
    FILE* f = fopen(path, "rb");
    if (!f) {
        return -errno;
    }
    size_t len = (size_t)st.st_size;
    char* body = calloc(len + 1, 1);
    if (!body) {
        (void)fclose(f);
        return -ENOMEM;
    }
    if (len > 0 && fread(body, 1, len, f) != len) {
        free(body);
        (void)fclose(f);
        return -EIO;
    }
    (void)fclose(f);
    body[len] = '\0';
    *out_body = body;
    *out_len = len;
    return 0;
}

static int restart_backoff_delay_sec(int attempt) {
    int safe_attempt = attempt < 0 ? 0 : attempt;
    int delay = RESTART_BACKOFF_BASE_SEC;
    for (int i = 0; i < safe_attempt; i++) {
        if (delay >= RESTART_BACKOFF_MAX_SEC) {
            return RESTART_BACKOFF_MAX_SEC;
        }
        if (delay > INT_MAX / 2) {
            return RESTART_BACKOFF_MAX_SEC;
        }
        delay *= 2;
    }
    if (delay > RESTART_BACKOFF_MAX_SEC) {
        delay = RESTART_BACKOFF_MAX_SEC;
    }
    return delay;
}

static void schedule_worker_restart(worker_state* w, const char* reason) {
    if (!w || !w->name) {
        return;
    }
    int delay_sec = restart_backoff_delay_sec(w->restart_attempt);
    w->restart_pending = true;
    w->restart_not_before_utc = time(NULL) + (time_t)delay_sec;
    if (w->restart_attempt < 30) {
        w->restart_attempt++;
    }
    ss_sdk_log_level level = SS_SDK_LOG_ERROR;
    if (reason && strncmp(reason, "control_", 8) == 0) {
        level = SS_SDK_LOG_WARN;
    }
    sdk_logf(level, "daemon.worker_restart_scheduled",
             "worker=%s reason=%s next_in_sec=%d attempt=%d", w->name, reason ? reason : "unknown", delay_sec,
             w->restart_attempt);
}

static int spawn_worker(worker_state* w, const char* cfg_path, const char* version) {
    if (!w || !w->binary || !w->name || !cfg_path || !version) {
        return -EINVAL;
    }

    pid_t p = fork();
    if (p < 0) {
        return -errno;
    }
    if (p == 0) {
        char parent_pid[32];
        (void)snprintf(parent_pid, sizeof(parent_pid), "%ld", (long)getppid());
        (void)setenv("SUNSPOTS_MASTER_CONFIG", cfg_path, 1);
        (void)setenv("SUNSPOTS_WORKER_NAME", w->name, 1);
        (void)setenv("SUNSPOTS_CONFIG_VERSION", version, 1);
        char* const argv[] = {w->binary, parent_pid, NULL};
        execv(w->binary, argv);
        _exit(127);
    }

    w->pid = p;
    w->last_heartbeat = time(NULL);
    w->restart_pending = false;
    w->restart_not_before_utc = 0;
    w->restart_attempt = 0;
    return 0;
}

static void stop_worker(worker_state* w) {
    if (!w || w->pid <= 0) {
        return;
    }
    pid_t pid = w->pid;
    (void)kill(pid, SIGTERM);

    int status = 0;
    const int max_wait_ms = 3000;
    int waited_ms = 0;
    while (waited_ms < max_wait_ms) {
        pid_t rc = waitpid(pid, &status, WNOHANG);
        if (rc == pid || (rc < 0 && errno == ECHILD)) {
            w->pid = 0;
            return;
        }
        if (rc < 0 && errno != EINTR) {
            break;
        }
        struct timespec ts = {.tv_sec = 0, .tv_nsec = 100 * 1000 * 1000};
        (void)nanosleep(&ts, NULL);
        waited_ms += 100;
    }

    (void)kill(pid, SIGKILL);
    (void)waitpid(pid, &status, 0);
    w->pid = 0;
}

static bool worker_uses_deadline_supervision(const worker_state* w) {
    return w && w->deadline_supervision;
}

static int json_int_or(const cJSON* obj, const char* key, int default_val) {
    const cJSON* n = cJSON_IsObject((cJSON*)obj) ? cJSON_GetObjectItemCaseSensitive((cJSON*)obj, key) : NULL;
    if (cJSON_IsNumber((cJSON*)n)) {
        return n->valueint;
    }
    return default_val;
}

static void assign_worker_supervision(const cJSON* worker_cfg, worker_state* out_worker) {
    if (!out_worker) {
        return;
    }

    out_worker->deadline_supervision = false;
    out_worker->interval_sec = 0;
    out_worker->slot_deadline_sec = 0;
    out_worker->grace_sec = 0;

    const cJSON* schedule = cJSON_IsObject((cJSON*)worker_cfg)
                                ? cJSON_GetObjectItemCaseSensitive((cJSON*)worker_cfg, "schedule")
                                : NULL;
    const cJSON* mode = cJSON_IsObject((cJSON*)schedule) ? cJSON_GetObjectItemCaseSensitive((cJSON*)schedule, "mode") : NULL;
    if (!cJSON_IsString((cJSON*)mode) || !mode->valuestring || strcmp(mode->valuestring, "interval_aligned") != 0) {
        return;
    }

    int interval_sec = json_int_or(schedule, "interval_sec", 900);
    int slot_deadline_sec = json_int_or(schedule, "slot_deadline_sec", 120);
    int grace_sec = json_int_or(schedule, "grace_sec", 90);
    if (interval_sec <= 0) {
        interval_sec = 900;
    }
    if (slot_deadline_sec < 0) {
        slot_deadline_sec = 120;
    }
    if (slot_deadline_sec > interval_sec) {
        slot_deadline_sec = interval_sec;
    }
    if (grace_sec < 0) {
        grace_sec = 90;
    }

    out_worker->deadline_supervision = true;
    out_worker->interval_sec = interval_sec;
    out_worker->slot_deadline_sec = slot_deadline_sec;
    out_worker->grace_sec = grace_sec;
}

static int discover_workers_from_root(const config* root, worker_state** out_workers, size_t* out_count) {
    if (!root || !out_workers || !out_count) {
        return -EINVAL;
    }

    char* workers_json = NULL;
    int rc = config_export_subtree_json(root, "workers", &workers_json);
    if (rc != 0 || !workers_json) {
        return -ENOENT;
    }

    cJSON* workers_obj = cJSON_Parse(workers_json);
    cJSON_free(workers_json);
    if (!cJSON_IsObject(workers_obj)) {
        cJSON_Delete(workers_obj);
        return -EINVAL;
    }

    size_t count = 0;
    cJSON* child = workers_obj->child;
    while (child) {
        if (cJSON_IsObject(child) && child->string && child->string[0] != '\0') {
            count++;
        }
        child = child->next;
    }

    worker_state* arr = calloc(count, sizeof(*arr));
    if (!arr) {
        cJSON_Delete(workers_obj);
        return -ENOMEM;
    }

    size_t idx = 0;
    child = workers_obj->child;
    while (child) {
        if (cJSON_IsObject(child) && child->string && child->string[0] != '\0') {
            const char* name = child->string;
            const cJSON* runtime = cJSON_GetObjectItemCaseSensitive(child, "runtime");
            const cJSON* runtime_binary = cJSON_IsObject((cJSON*)runtime)
                                              ? cJSON_GetObjectItemCaseSensitive((cJSON*)runtime, "binary")
                                              : NULL;

            char binary_buf[512];
            const char* binary = NULL;
            if (cJSON_IsString((cJSON*)runtime_binary) && runtime_binary->valuestring && runtime_binary->valuestring[0] != '\0') {
                binary = runtime_binary->valuestring;
            } else {
                bool looks_like_fetch = cJSON_GetObjectItemCaseSensitive(child, "request") != NULL &&
                                        cJSON_GetObjectItemCaseSensitive(child, "mapping") != NULL;
                bool looks_like_server = cJSON_GetObjectItemCaseSensitive(child, "listen_port") != NULL ||
                                         cJSON_GetObjectItemCaseSensitive(child, "listen_host") != NULL;
                if (looks_like_fetch) {
                    binary = "build/bin/fetch_worker";
                } else if (looks_like_server) {
                    binary = "build/bin/sunspots_server";
                } else {
                    (void)snprintf(binary_buf, sizeof(binary_buf), "build/bin/%s", name);
                    binary = binary_buf;
                }
            }

            arr[idx].name = dupstr(name);
            arr[idx].binary = dupstr(binary);
            assign_worker_supervision(child, &arr[idx]);
            if (!arr[idx].name || !arr[idx].binary) {
                for (size_t j = 0; j <= idx; j++) {
                    worker_free(&arr[j]);
                }
                free(arr);
                cJSON_Delete(workers_obj);
                return -ENOMEM;
            }
            idx++;
        }
        child = child->next;
    }

    cJSON_Delete(workers_obj);
    *out_workers = arr;
    *out_count = idx;
    return 0;
}

static unsigned long djb2_hash(const char* s, unsigned long seed) {
    unsigned long h = seed;
    for (size_t i = 0; s && s[i] != '\0'; i++) {
        h = ((h << 5U) + h) + (unsigned long)(unsigned char)s[i];
    }
    return h;
}

static int compute_config_version(const config* root, char* out_version, size_t out_version_sz) {
    if (!root || !out_version || out_version_sz < 17) {
        return -EINVAL;
    }

    char* root_json = NULL;
    int rc = config_export_subtree_json(root, "", &root_json);
    if (rc != 0 || !root_json) {
        return -EINVAL;
    }

    unsigned long hash = djb2_hash(root_json, 5381UL);
    cJSON_free(root_json);
    (void)snprintf(out_version, out_version_sz, "%016lx", hash);
    return 0;
}

static worker_state* find_worker_by_pid(worker_state* workers, size_t workers_count, pid_t pid) {
    if (!workers || pid <= 0) {
        return NULL;
    }
    for (size_t i = 0; i < workers_count; i++) {
        if (workers[i].pid == pid) {
            return &workers[i];
        }
    }
    return NULL;
}

static worker_state* find_worker_by_name(worker_state* workers, size_t workers_count, const char* name) {
    if (!workers || !name || name[0] == '\0') {
        return NULL;
    }
    for (size_t i = 0; i < workers_count; i++) {
        if (workers[i].name && strcmp(workers[i].name, name) == 0) {
            return &workers[i];
        }
    }
    return NULL;
}

static void check_deadline_supervision_workers(worker_state* workers, size_t workers_count, const char* cfg_path,
                                               const char* version) {
    (void)cfg_path;
    (void)version;
    for (size_t i = 0; i < workers_count; i++) {
        worker_state* w = &workers[i];
        if (w->pid <= 0 || w->restart_pending || !worker_uses_deadline_supervision(w)) {
            continue;
        }

        time_t now = time(NULL);
        if (w->deadline_supervision_resume_utc > 0 && now < w->deadline_supervision_resume_utc) {
            continue;
        }
        time_t slot_start_utc = ss_current_aligned_slot_start(now, w->interval_sec);
        if (slot_start_utc < 0) {
            continue;
        }

        time_t slot_deadline_utc = 0;
        time_t supervisor_deadline_utc = 0;
        if (ss_compute_slot_window(slot_start_utc, w->slot_deadline_sec, w->grace_sec, &slot_deadline_utc,
                                   &supervisor_deadline_utc) != 0) {
            continue;
        }

        char status_path[512];
        (void)snprintf(status_path, sizeof(status_path), "runtime/status/%s.json", w->name);

        ss_worker_status status;
        bool slot_success = false;
        if (ss_status_read_file(status_path, &status) == 0) {
            slot_success = ss_status_is_slot_success(&status, w->name, slot_start_utc);
        }

        ss_supervision_decision decision = ss_supervision_evaluate(now, supervisor_deadline_utc, slot_success);
        if (decision != SS_SUPERVISION_RESTART) {
            continue;
        }
        if (w->last_deadline_restart_slot_utc == slot_start_utc) {
            continue;
        }

        stop_worker(w);
        w->last_deadline_restart_slot_utc = slot_start_utc;
        schedule_worker_restart(w, "supervisor_deadline_missed");
    }
}

static void check_stale_workers(worker_state* workers, size_t workers_count, int timeout_sec, const char* cfg_path,
                                const char* version) {
    (void)cfg_path;
    (void)version;
    time_t now = time(NULL);
    for (size_t i = 0; i < workers_count; i++) {
        worker_state* w = &workers[i];
        if (w->pid <= 0 || w->restart_pending) {
            continue;
        }
        if (worker_uses_deadline_supervision(w)) {
            continue;
        }
        if ((now - w->last_heartbeat) <= timeout_sec) {
            continue;
        }
        stop_worker(w);
        schedule_worker_restart(w, "heartbeat_timeout");
    }
}

static bool reap_and_respawn(worker_state* workers, size_t workers_count, const char* cfg_path, const char* version) {
    (void)cfg_path;
    (void)version;
    int status = 0;
    bool unexpected_exit = false;
    for (;;) {
        pid_t dead = waitpid(-1, &status, WNOHANG);
        if (dead <= 0) {
            return unexpected_exit;
        }
        worker_state* w = find_worker_by_pid(workers, workers_count, dead);
        if (w) {
            w->pid = 0;
            if (!w->restart_pending) {
                schedule_worker_restart(w, "worker_exited");
                unexpected_exit = true;
            }
        }
    }
}

static void process_pending_restarts(worker_state* workers, size_t workers_count, const char* cfg_path, const char* version) {
    time_t now = time(NULL);
    for (size_t i = 0; i < workers_count; i++) {
        worker_state* w = &workers[i];
        if (!w->restart_pending || w->pid > 0) {
            continue;
        }
        if (now < w->restart_not_before_utc) {
            continue;
        }
        int rc = spawn_worker(w, cfg_path, version);
        if (rc == 0) {
            sdk_logf(SS_SDK_LOG_WARN, "daemon.worker_restarted", "worker=%s restarted after backoff", w->name);
        } else {
            schedule_worker_restart(w, "restart_spawn_failed");
        }
    }
}

static bool config_watch_triggered(int inotify_fd, int watch_desc, const char* target_file) {
    if (inotify_fd < 0 || watch_desc < 0 || !target_file) {
        return false;
    }

    char buf[4096];
    bool changed = false;
    ssize_t n = 0;
    do {
        n = read(inotify_fd, buf, sizeof(buf));
        if (n <= 0) {
            break;
        }
        size_t off = 0;
        while (off + sizeof(struct inotify_event) <= (size_t)n) {
            const struct inotify_event* ev = (const struct inotify_event*)(buf + off);
            if (ev->wd == watch_desc && ev->len > 0 && strcmp(ev->name, target_file) == 0) {
                changed = true;
            }
            off += sizeof(struct inotify_event) + (size_t)ev->len;
        }
    } while (n > 0);

    if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
        return false;
    }
    return changed;
}

static int set_sdk_config_env(const config* root) {
    char* cfg_json = NULL;
    if (config_export_subtree_json(root, "", &cfg_json) == 0 && cfg_json != NULL) {
        int rc = setenv("SUNSPOTS_CONFIG", cfg_json, 1);
        cJSON_free(cfg_json);
        if (rc != 0) {
            return -errno;
        }
    }
    return 0;
}

static int load_root_config(const char* cfg_path, config** out_root, const config** out_common) {
    if (!cfg_path || !out_root || !out_common) {
        return -EINVAL;
    }
    config* root = config_create();
    if (!root) {
        return -ENOMEM;
    }
    int rc = config_load_file(root, cfg_path);
    if (rc != 0) {
        config_destroy(&root);
        return rc;
    }
    const config* common = config_get_subtree(root, "common");
    if (!common) {
        config_destroy(&root);
        return -ENOENT;
    }
    *out_root = root;
    *out_common = common;
    return 0;
}

static int reconcile_workers(const char* cfg_path, config* root, worker_state** io_workers, size_t* io_workers_count,
                             char* io_version, size_t version_sz) {
    if (!cfg_path || !root || !io_workers || !io_workers_count || !io_version || version_sz == 0) {
        return -EINVAL;
    }

    worker_state* discovered = NULL;
    size_t discovered_count = 0;
    int rc = discover_workers_from_root(root, &discovered, &discovered_count);
    if (rc != 0) {
        return rc;
    }

    char new_version[32] = {0};
    rc = compute_config_version(root, new_version, sizeof(new_version));
    if (rc != 0) {
        workers_free(&discovered, &discovered_count);
        return rc;
    }

    for (size_t i = 0; i < *io_workers_count; i++) {
        stop_worker(&(*io_workers)[i]);
    }

    for (size_t i = 0; i < discovered_count; i++) {
        rc = spawn_worker(&discovered[i], cfg_path, new_version);
        if (rc != 0) {
            sdk_logf(SS_SDK_LOG_ERROR, "daemon.spawn_failed", "failed to spawn worker=%s", discovered[i].name);
            schedule_worker_restart(&discovered[i], "initial_spawn_failed");
        } else {
            sdk_logf(SS_SDK_LOG_INFO, "daemon.spawned", "spawned worker=%s pid=%ld", discovered[i].name,
                     (long)discovered[i].pid);
        }
    }

    workers_free(io_workers, io_workers_count);
    *io_workers = discovered;
    *io_workers_count = discovered_count;
    (void)snprintf(io_version, version_sz, "%s", new_version);
    return 0;
}

static int reload_config_and_reconcile(const char* cfg_path, config** io_root, const config** io_common,
                                       worker_state** io_workers, size_t* io_workers_count, char* io_version,
                                       size_t version_sz, int* io_timeout_sec) {
    config* new_root = NULL;
    const config* new_common = NULL;
    int lrc = load_root_config(cfg_path, &new_root, &new_common);
    if (lrc != 0) {
        return lrc;
    }
    if (set_sdk_config_env(new_root) != 0) {
        config_destroy(&new_root);
        return -EINVAL;
    }
    int rrc = reconcile_workers(cfg_path, new_root, io_workers, io_workers_count, io_version, version_sz);
    if (rrc != 0) {
        config_destroy(&new_root);
        return rrc;
    }

    config_destroy(io_root);
    *io_root = new_root;
    *io_common = new_common;
    if (io_timeout_sec) {
        int t = config_get_int_or(*io_common, "heartbeat.timeout_sec", 60);
        if (t <= 0) {
            t = 60;
        }
        *io_timeout_sec = t;
    }
    return 0;
}

static int clear_status_files(void) {
    DIR* d = opendir("runtime/status");
    if (!d) {
        if (errno == ENOENT) {
            return 0;
        }
        return -errno;
    }
    int rc = 0;
    struct dirent* ent = NULL;
    while ((ent = readdir(d)) != NULL) {
        if (!ent->d_name || ent->d_name[0] == '\0') {
            continue;
        }
        size_t n = strlen(ent->d_name);
        if (n < 6 || strcmp(ent->d_name + n - 5, ".json") != 0) {
            continue;
        }
        char p[768];
        int m = snprintf(p, sizeof(p), "runtime/status/%s", ent->d_name);
        if (m <= 0 || (size_t)m >= sizeof(p)) {
            continue;
        }
        if (unlink(p) != 0 && errno != ENOENT) {
            rc = -errno;
        }
    }
    closedir(d);
    return rc;
}

static int handle_control_command(const char* cfg_path, config** io_root, const config** io_common,
                                  worker_state** io_workers, size_t* io_workers_count, char* io_version,
                                  size_t version_sz, int* io_timeout_sec, bool* out_stop_daemon) {
    if (!cfg_path || !io_root || !*io_root || !io_common || !*io_common || !io_workers || !io_workers_count ||
        !io_version || !out_stop_daemon) {
        return -EINVAL;
    }
    *out_stop_daemon = false;

    char* body = NULL;
    size_t body_len = 0;
    int rc = read_text_file_limited(CONTROL_COMMAND_PATH, 65536, &body, &body_len);
    if (rc != 0) {
        if (rc == -ENOENT) {
            return 0;
        }
        return rc;
    }

    cJSON* cmd = cJSON_ParseWithLength(body, body_len);
    free(body);
    (void)unlink(CONTROL_COMMAND_PATH);
    if (!cJSON_IsObject(cmd)) {
        cJSON_Delete(cmd);
        return -EINVAL;
    }

    cJSON* action = cJSON_GetObjectItemCaseSensitive(cmd, "action");
    cJSON* worker_name = cJSON_GetObjectItemCaseSensitive(cmd, "worker");
    if (!cJSON_IsString(action) || !action->valuestring || action->valuestring[0] == '\0') {
        cJSON_Delete(cmd);
        return -EINVAL;
    }

    const char* a = action->valuestring;
    if (strcmp(a, "reload_config") == 0) {
        rc = reload_config_and_reconcile(cfg_path, io_root, io_common, io_workers, io_workers_count, io_version,
                                         version_sz, io_timeout_sec);
        if (rc == 0) {
            sdk_logf(SS_SDK_LOG_INFO, "daemon.control.reload", "reload requested workers=%zu version=%s",
                     *io_workers_count, io_version);
        }
    } else if (strcmp(a, "flush_db") == 0) {
        rc = flush_db_file(*io_common);
        if (rc == 0) {
            sdk_logf(SS_SDK_LOG_WARN, "daemon.control.db_flushed", "db flushed by control command");
        }
    } else if (strcmp(a, "clear_logs") == 0) {
        rc = clear_log_file(*io_common);
        if (rc == 0) {
            sdk_logf(SS_SDK_LOG_WARN, "daemon.control.logs_cleared", "logs cleared by control command");
        }
    } else if (strcmp(a, "clean_restart") == 0) {
        rc = clear_status_files();
        if (rc == 0) {
            rc = clear_log_file(*io_common);
        }
        if (rc == 0) {
            rc = flush_db_file(*io_common);
        }
        if (rc == 0) {
            time_t now = time(NULL);
            for (size_t i = 0; i < *io_workers_count; i++) {
                stop_worker(&(*io_workers)[i]);
                (*io_workers)[i].restart_pending = true;
                (*io_workers)[i].restart_not_before_utc = now;
                (*io_workers)[i].restart_attempt = 0;
                /* Give worker one full supervision window after clean reset before enforcing deadline restarts. */
                (*io_workers)[i].deadline_supervision_resume_utc =
                    now + (time_t)((*io_workers)[i].slot_deadline_sec + (*io_workers)[i].grace_sec + 5);
            }
            sdk_logf(SS_SDK_LOG_WARN, "daemon.control.clean_restart",
                     "status/log/db reset and worker restart scheduled");
        }
    } else if (strcmp(a, "stop_daemon") == 0) {
        *out_stop_daemon = true;
        rc = 0;
    } else if (strcmp(a, "restart_all_workers") == 0) {
        for (size_t i = 0; i < *io_workers_count; i++) {
            stop_worker(&(*io_workers)[i]);
            schedule_worker_restart(&(*io_workers)[i], "control_restart_all");
            (*io_workers)[i].restart_not_before_utc = time(NULL);
        }
        rc = 0;
    } else if (strcmp(a, "clear_status") == 0) {
        rc = clear_status_files();
    } else if (strcmp(a, "worker_restart") == 0 || strcmp(a, "worker_stop") == 0 || strcmp(a, "worker_start") == 0) {
        if (!cJSON_IsString(worker_name) || !worker_name->valuestring || worker_name->valuestring[0] == '\0') {
            rc = -EINVAL;
        } else {
            worker_state* w = find_worker_by_name(*io_workers, *io_workers_count, worker_name->valuestring);
            if (!w) {
                rc = -ENOENT;
            } else if (strcmp(a, "worker_restart") == 0) {
                stop_worker(w);
                schedule_worker_restart(w, "control_worker_restart");
                w->restart_not_before_utc = time(NULL);
                rc = 0;
            } else if (strcmp(a, "worker_stop") == 0) {
                stop_worker(w);
                w->restart_pending = false;
                w->restart_not_before_utc = 0;
                rc = 0;
            } else {
                if (w->pid <= 0) {
                    rc = spawn_worker(w, cfg_path, io_version);
                    if (rc != 0) {
                        schedule_worker_restart(w, "control_worker_start_failed");
                    }
                } else {
                    rc = 0;
                }
            }
        }
    } else {
        rc = -EINVAL;
    }

    cJSON_Delete(cmd);
    return rc;
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    const char* cfg_path = "config/sunspots.json";

    int rc = ensure_runtime_dirs();
    if (rc != 0) {
        return EXIT_FAILURE;
    }
    rc = acquire_daemon_lock();
    if (rc != 0) {
        sdk_logf(SS_SDK_LOG_ERROR, "daemon.lock_failed", "another daemon may already be running rc=%d", rc);
        return EXIT_FAILURE;
    }
    cleanup_orphan_runtime_processes();

    config* root = NULL;
    const config* common = NULL;
    rc = load_root_config(cfg_path, &root, &common);
    if (rc != 0) {
        release_daemon_lock();
        return EXIT_FAILURE;
    }

    if (set_sdk_config_env(root) != 0) {
        config_destroy(&root);
        release_daemon_lock();
        return EXIT_FAILURE;
    }

    worker_state* workers = NULL;
    size_t workers_count = 0;
    char version[32] = {0};

    if (config_get_bool_or(common, "db.flush_on_startup", false)) {
        int frc = flush_db_file(common);
        if (frc == 0) {
            sdk_logf(SS_SDK_LOG_WARN, "daemon.db.flush_on_startup", "database flushed on startup");
        } else {
            sdk_logf(SS_SDK_LOG_ERROR, "daemon.db.flush_on_startup_failed", "flush_on_startup failed rc=%d", frc);
        }
    }

    rc = reconcile_workers(cfg_path, root, &workers, &workers_count, version, sizeof(version));
    if (rc != 0) {
        sdk_logf(SS_SDK_LOG_ERROR, "daemon.runtime_slice.build_failed", "initial reconcile failed rc=%d", rc);
        config_destroy(&root);
        release_daemon_lock();
        return EXIT_FAILURE;
    }
    sdk_logf(SS_SDK_LOG_INFO, "daemon.start", "daemon start workers=%zu version=%s", workers_count, version);

    int inotify_fd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
    int config_wd = -1;
    if (inotify_fd >= 0) {
        config_wd = inotify_add_watch(inotify_fd, "config", IN_CLOSE_WRITE | IN_MOVED_TO | IN_CREATE | IN_DELETE | IN_MODIFY);
    }

    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, SIGRTMIN);
    sigaddset(&set, SIGCHLD);
    sigaddset(&set, SIGTERM);
    sigaddset(&set, SIGINT);
    sigaddset(&set, SIGUSR1);
    sigaddset(&set, SIGUSR2);
    if (sigprocmask(SIG_BLOCK, &set, NULL) != 0) {
        workers_free(&workers, &workers_count);
        if (config_wd >= 0) {
            (void)inotify_rm_watch(inotify_fd, config_wd);
        }
        if (inotify_fd >= 0) {
            close(inotify_fd);
        }
        config_destroy(&root);
        release_daemon_lock();
        return EXIT_FAILURE;
    }

    bool running = true;
    int timeout_sec = config_get_int_or(common, "heartbeat.timeout_sec", 60);
    if (timeout_sec <= 0) {
        timeout_sec = 60;
    }

    while (running) {
        siginfo_t si;
        struct timespec timeout = {.tv_sec = 1, .tv_nsec = 0};
        int sig = sigtimedwait(&set, &si, &timeout);
        if (sig == -1) {
            if (errno == EAGAIN) {
                if (config_watch_triggered(inotify_fd, config_wd, "sunspots.json")) {
                    config* new_root = NULL;
                    const config* new_common = NULL;
                    int lrc = load_root_config(cfg_path, &new_root, &new_common);
                    if (lrc == 0 && set_sdk_config_env(new_root) == 0) {
                        int rrc = reconcile_workers(cfg_path, new_root, &workers, &workers_count, version,
                                                    sizeof(version));
                        if (rrc == 0) {
                            config_destroy(&root);
                            root = new_root;
                            common = new_common;
                            timeout_sec = config_get_int_or(common, "heartbeat.timeout_sec", 60);
                            if (timeout_sec <= 0) {
                                timeout_sec = 60;
                            }
                            sdk_logf(SS_SDK_LOG_INFO, "daemon.config.reload", "config reloaded workers=%zu version=%s",
                                     workers_count, version);
                        } else {
                            sdk_logf(SS_SDK_LOG_ERROR, "daemon.config.reload_failed",
                                     "reconcile failed on config reload rc=%d", rrc);
                            config_destroy(&new_root);
                        }
                    } else {
                        sdk_logf(SS_SDK_LOG_ERROR, "daemon.config.reload_failed", "failed to load new config");
                        if (new_root) {
                            config_destroy(&new_root);
                        }
                    }
                }

                check_deadline_supervision_workers(workers, workers_count, cfg_path, version);
                check_stale_workers(workers, workers_count, timeout_sec, cfg_path, version);
                process_pending_restarts(workers, workers_count, cfg_path, version);
                continue;
            }
            break;
        }

        if (sig == SIGTERM || sig == SIGINT) {
            running = false;
            continue;
        }
        if (sig == SIGUSR1) {
            bool stop_daemon = false;
            int crc = handle_control_command(cfg_path, &root, &common, &workers, &workers_count, version, sizeof(version),
                                             &timeout_sec, &stop_daemon);
            if (crc == -ENOENT) {
                int rrc = reload_config_and_reconcile(cfg_path, &root, &common, &workers, &workers_count, version,
                                                      sizeof(version), &timeout_sec);
                if (rrc == 0) {
                    sdk_logf(SS_SDK_LOG_INFO, "daemon.control.reload", "reload requested workers=%zu version=%s",
                             workers_count, version);
                } else {
                    sdk_logf(SS_SDK_LOG_ERROR, "daemon.control.reload_failed", "reconcile failed rc=%d", rrc);
                }
            } else if (crc != 0) {
                sdk_logf(SS_SDK_LOG_ERROR, "daemon.control.command_failed", "control command failed rc=%d", crc);
            }
            if (stop_daemon) {
                running = false;
            }
            continue;
        }
        if (sig == SIGUSR2) {
            int frc = flush_db_file(common);
            if (frc == 0) {
                sdk_logf(SS_SDK_LOG_WARN, "daemon.control.db_flushed", "db flushed by control signal");
            } else {
                sdk_logf(SS_SDK_LOG_ERROR, "daemon.control.db_flush_failed", "db flush failed rc=%d", frc);
            }
            continue;
        }
        if (sig == SIGCHLD) {
            bool unexpected_exit = reap_and_respawn(workers, workers_count, cfg_path, version);
            process_pending_restarts(workers, workers_count, cfg_path, version);
            if (unexpected_exit) {
                sdk_logf(SS_SDK_LOG_WARN, "daemon.worker_exit", "worker exit detected; restart is backoff-scheduled");
            }
            continue;
        }
        if (sig == SIGRTMIN) {
            worker_state* w = find_worker_by_pid(workers, workers_count, si.si_pid);
            if (w) {
                w->last_heartbeat = time(NULL);
            }
        }
    }

    for (size_t i = 0; i < workers_count; i++) {
        stop_worker(&workers[i]);
    }
    cleanup_orphan_runtime_processes();
    workers_free(&workers, &workers_count);

    if (config_wd >= 0) {
        (void)inotify_rm_watch(inotify_fd, config_wd);
    }
    if (inotify_fd >= 0) {
        close(inotify_fd);
    }

    sdk_logf(SS_SDK_LOG_INFO, "daemon.shutdown", "daemon shutdown");
    ss_sdk_shutdown();
    config_destroy(&root);
    release_daemon_lock();
    return EXIT_SUCCESS;
}
