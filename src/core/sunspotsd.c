#include <errno.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "config/config.h"
#include "core/config_runtime.h"
#include "log/sunspots_log.h"

typedef struct worker_state {
    const char* name;
    const char* binary;
    const char* subtree;
    const char* slice_file;
    pid_t pid;
    time_t last_heartbeat;
} worker_state;

static worker_state g_workers[] = {
    {.name = "fetch_openmeteo",
     .binary = "build/bin/fetch_openmeteo",
     .subtree = "workers.fetch_openmeteo",
     .slice_file = "runtime/config/fetch_openmeteo.json"},
    {.name = "fetch_smhi",
     .binary = "build/bin/fetch_smhi",
     .subtree = "workers.fetch_smhi",
     .slice_file = "runtime/config/fetch_smhi.json"},
    {.name = "calc_smhi_avg_temp",
     .binary = "build/bin/calc_smhi_avg_temp",
     .subtree = "workers.calc_smhi_avg_temp",
     .slice_file = "runtime/config/calc_smhi_avg_temp.json"},
    {.name = "server",
     .binary = "build/bin/sunspots_server",
     .subtree = "workers.server",
     .slice_file = "runtime/config/server.json"},
};

static int ensure_runtime_dirs(void) {
    if (mkdir("runtime", 0755) != 0 && errno != EEXIST) {
        return -errno;
    }
    if (mkdir("runtime/config", 0755) != 0 && errno != EEXIST) {
        return -errno;
    }
    return 0;
}

static int spawn_worker(worker_state* w, const char* version) {
    if (!w || !version) {
        return -EINVAL;
    }

    pid_t p = fork();
    if (p < 0) {
        return -errno;
    }
    if (p == 0) {
        char parent_pid[32];
        (void) snprintf(parent_pid, sizeof(parent_pid), "%ld", (long) getppid());
        (void) setenv("SUNSPOTS_CONFIG_PATH", w->slice_file, 1);
        (void) setenv("SUNSPOTS_CONFIG_VERSION", version, 1);
        char* const argv[] = {(char*) w->binary, parent_pid, NULL};
        execv(w->binary, argv);
        _exit(127);
    }

    w->pid = p;
    w->last_heartbeat = time(NULL);
    return 0;
}

static worker_state* find_worker_by_pid(pid_t pid) {
    if (pid <= 0) {
        return NULL;
    }
    size_t workers_count = sizeof(g_workers) / sizeof(g_workers[0]);
    for (size_t i = 0; i < workers_count; i++) {
        if (g_workers[i].pid == pid) {
            return &g_workers[i];
        }
    }
    return NULL;
}

static void reap_and_respawn(const char* version) {
    int status = 0;
    for (;;) {
        pid_t dead = waitpid(-1, &status, WNOHANG);
        if (dead <= 0) {
            return;
        }
        worker_state* w = find_worker_by_pid(dead);
        if (w) {
            (void) spawn_worker(w, version);
        }
    }
}

static void check_stale_workers(int timeout_sec, const char* version) {
    time_t now = time(NULL);
    size_t workers_count = sizeof(g_workers) / sizeof(g_workers[0]);
    for (size_t i = 0; i < workers_count; i++) {
        worker_state* w = &g_workers[i];
        if (w->pid <= 0) {
            continue;
        }
        if ((now - w->last_heartbeat) <= timeout_sec) {
            continue;
        }
        (void) kill(w->pid, SIGTERM);
        (void) kill(w->pid, SIGKILL);
        (void) waitpid(w->pid, NULL, 0);
        (void) spawn_worker(w, version);
    }
}

int main(int argc, char** argv) {
    (void) argc;
    (void) argv;
    const char* cfg_path = "config/sunspots.json";

    int rc = ensure_runtime_dirs();
    if (rc != 0) {
        return EXIT_FAILURE;
    }

    config* root = config_create();
    if (!root) {
        return EXIT_FAILURE;
    }
    rc = config_load_file(root, cfg_path);
    if (rc != 0) {
        config_destroy(&root);
        return EXIT_FAILURE;
    }

    const config* common = config_get_subtree(root, "common");
    if (!common) {
        config_destroy(&root);
        return EXIT_FAILURE;
    }
    char log_path[512];
    sunspots_log* log = NULL;
    if (config_get_string(common, "paths.log_file", log_path, sizeof(log_path)) == 0) {
        (void) sunspots_log_open(&log, log_path, "daemon", "", "boot");
    }

    size_t workers_count = sizeof(g_workers) / sizeof(g_workers[0]);
    worker_cfg_target targets[8];
    if (workers_count > (sizeof(targets) / sizeof(targets[0]))) {
        config_destroy(&root);
        return EXIT_FAILURE;
    }
    for (size_t i = 0; i < workers_count; i++) {
        targets[i].worker_name = g_workers[i].name;
        targets[i].subtree_path = g_workers[i].subtree;
        targets[i].out_file = g_workers[i].slice_file;
    }

    char version[32];
    rc = cfg_runtime_build_all(root, targets, workers_count, common, version, sizeof(version));
    if (rc != 0) {
        (void) SUNSPOTS_LOG_ERROR(log, "daemon", "failed to build runtime config slices: rc=%d", rc);
        config_destroy(&root);
        sunspots_log_close(&log);
        return EXIT_FAILURE;
    }
    if (log) {
        sunspots_log_close(&log);
        (void) sunspots_log_open(&log, log_path, "daemon", "", version);
        (void) SUNSPOTS_LOG_INFO(log, "daemon", "daemon start; workers=%zu", workers_count);
    }

    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, SIGRTMIN);
    sigaddset(&set, SIGCHLD);
    sigaddset(&set, SIGTERM);
    sigaddset(&set, SIGINT);
    if (sigprocmask(SIG_BLOCK, &set, NULL) != 0) {
        config_destroy(&root);
        return EXIT_FAILURE;
    }

    for (size_t i = 0; i < workers_count; i++) {
        if (spawn_worker(&g_workers[i], version) != 0) {
            (void) SUNSPOTS_LOG_ERROR(log, "daemon", "failed to spawn worker=%s", g_workers[i].name);
            config_destroy(&root);
            sunspots_log_close(&log);
            return EXIT_FAILURE;
        }
        (void) SUNSPOTS_LOG_INFO(log, "daemon", "spawned worker=%s pid=%ld", g_workers[i].name,
                                 (long) g_workers[i].pid);
    }

    bool running = true;
    int timeout_sec = config_get_int_or(common, "heartbeat.timeout_sec", 10);
    if (timeout_sec <= 0) {
        timeout_sec = 10;
    }

    while (running) {
        siginfo_t si;
        struct timespec timeout = {.tv_sec = 1, .tv_nsec = 0};
        int sig = sigtimedwait(&set, &si, &timeout);
        if (sig == -1) {
            if (errno == EAGAIN) {
                check_stale_workers(timeout_sec, version);
                continue;
            }
            break;
        }

        if (sig == SIGTERM || sig == SIGINT) {
            running = false;
            continue;
        }
        if (sig == SIGCHLD) {
            reap_and_respawn(version);
            (void) SUNSPOTS_LOG_WARN(log, "daemon", "worker exit detected and respawn attempted");
            continue;
        }
        if (sig == SIGRTMIN) {
            worker_state* w = find_worker_by_pid(si.si_pid);
            if (w) {
                w->last_heartbeat = time(NULL);
            }
        }
    }

    for (size_t i = 0; i < workers_count; i++) {
        if (g_workers[i].pid > 0) {
            (void) kill(g_workers[i].pid, SIGTERM);
            (void) waitpid(g_workers[i].pid, NULL, 0);
        }
    }
    (void) SUNSPOTS_LOG_INFO(log, "daemon", "daemon shutdown");
    sunspots_log_close(&log);
    config_destroy(&root);
    return EXIT_SUCCESS;
}
