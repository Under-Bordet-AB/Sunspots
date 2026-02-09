#define _POSIX_C_SOURCE 200809L
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <syslog.h>
#include <unistd.h>
#include <cJSON.h>

#include "fetch_utils.h"

#define HEARTBEAT_TIMEOUT 10
#define MAX_APIS 8

typedef struct api {
    char* name;
    char* path;
    int interval;
    pid_t pid;
    time_t last_heartbeat;
} api;

int g_api_count = 0;
api g_apis[MAX_APIS];

pid_t g_parent_pid = 0;
int g_heartbeat_freq = 0;

void* heartbeat();
void handle_child_heartbeat(int sig, siginfo_t* info, void* context);
int load_apis_from_json(const char* path);
void cleanup(void);

int main(int argc, char* argv[]) {
    atexit(cleanup);

    openlog("SUNSPOTS_FETCH_MANAGER", LOG_PID, LOG_DAEMON);
    
    if (argc < 3) {
        syslog(LOG_ERR, "Fetch Manager - Usage: ./path/to/bin <PPID> <Heartbeat frequency in seconds>");
        exit(EXIT_FAILURE);
    }
    
    syslog(LOG_INFO, "Fetch Manager - Starting...");

    // Parse arguments
    char* endptr;
    g_parent_pid = (int)strtol(argv[1], &endptr, 10);
    if (*endptr != '\0') {
        exit(EXIT_FAILURE);
    }
    g_heartbeat_freq = (int)strtol(argv[2], &endptr, 10);
    if (*endptr != '\0') {
        exit(EXIT_FAILURE);
    }

    // setup();
    if (load_apis_from_json("fetch_manager_config.json") < 0) {
        syslog(LOG_ERR, "Fetch Manager - Couldn't load config file.");
        exit(EXIT_FAILURE);
    }

    // Set up signal handler for child heartbeats
    struct sigaction sa;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_SIGINFO;
    sa.sa_sigaction = handle_child_heartbeat;
    sigaction(SIGRTMIN, &sa, NULL);

    pthread_t thread_heartbeat;
    pthread_create(&thread_heartbeat, NULL, (void* (*) (void*)) heartbeat, NULL);
    pthread_detach(thread_heartbeat);

    while (1) {
        char parent_pid_str[16];
        snprintf(parent_pid_str, sizeof(parent_pid_str), "%d", getpid());

        for (int i = 0; i < g_api_count; i++) {
            // Spawn process
            if (g_apis[i].pid == 0) {
                syslog(LOG_INFO, "Fetch Manager - Spawning %s worker...", g_apis[i].name);
                g_apis[i].pid = fork();
                if (g_apis[i].pid == -1) {
                    perror("fork");
                    exit(EXIT_FAILURE);
                } else if (g_apis[i].pid == 0) {
                    execv(g_apis[i].path, (char* const[]){g_apis[i].path, parent_pid_str, NULL});
                    perror("execv");
                    exit(EXIT_FAILURE);
                }
                g_apis[i].last_heartbeat = time(NULL);
            }

            time_t now = time(NULL);

            // Check for timeouts
            if (g_apis[i].pid > 0 && now - g_apis[i].last_heartbeat > HEARTBEAT_TIMEOUT) {
                syslog(LOG_WARNING, "Fetch Manager - %s process timeout, killing...", g_apis[i].name);
                kill(g_apis[i].pid, SIGKILL);
                waitpid(g_apis[i].pid, NULL, 0);
                g_apis[i].pid = 0;
            }

            // Check for unexpected exits
            if (g_apis[i].pid > 0 && waitpid(g_apis[i].pid, NULL, WNOHANG) > 0) {
                syslog(LOG_WARNING, "Fetch Manager - %s process exited unexpectedly", g_apis[i].name);
                g_apis[i].pid = 0;
            }
        }

        sleep(2);
    }

    closelog();

    return 0;
}

int load_apis_from_json(const char* path) {
    char* json = read_file_to_string(path);
    if (!json) {
        syslog(LOG_WARNING, "Fetch manager - JSON config could not be loaded");
        return -1;
    }

    cJSON* root = cJSON_Parse(json);
    free(json);
    if (!root) {
        syslog(LOG_WARNING, "Fetch manager - JSON config could not be parsed");
        return -1;
    }

    cJSON* apis_json = cJSON_GetObjectItemCaseSensitive(root, "apis");
    if (!cJSON_IsArray(apis_json)) {
        syslog(LOG_WARNING, "Fetch manager - Could not parse JSON root object");
        cJSON_Delete(root);
        return -1;
    }

    int count = cJSON_GetArraySize(apis_json);
    if (count > MAX_APIS) {
        syslog(LOG_WARNING, "Fetch Manager - Too many APIs found in config file.");
        count = MAX_APIS;
    }

    int out = 0;
    for (int i = 0; i < count; i++) {
        cJSON* item = cJSON_GetArrayItem(apis_json, i);
        cJSON* name = cJSON_GetObjectItemCaseSensitive(item, "name");
        cJSON* bin = cJSON_GetObjectItemCaseSensitive(item, "bin_path");
        cJSON* interval = cJSON_GetObjectItemCaseSensitive(item, "interval");

        if (!cJSON_IsString(name) || !cJSON_IsString(bin) || !cJSON_IsNumber(interval)) {
            syslog(LOG_WARNING, "Fetch Manager - Invalid JSON object found in config.");
            continue;
        }

        g_apis[out].name = strdup(name->valuestring);
        g_apis[out].path = strdup(bin->valuestring);
        g_apis[out].interval = interval->valueint;
        g_apis[out].pid = 0;
        g_apis[out].last_heartbeat = time(NULL);

        out++;
    }

    g_api_count = out;

    cJSON_Delete(root);

    return 0;
}

void* heartbeat() {
    while (1) {
        if (kill(g_parent_pid, SIGRTMIN) == -1) {
            perror("Could not signal daemon, terminating.\n");
            exit(EXIT_FAILURE);
        }
        syslog(LOG_INFO, "Fetch Manager - Beating...");
        sleep(g_heartbeat_freq);
    }

    return NULL;
}

void handle_child_heartbeat(int sig, siginfo_t* info, void* context) {
    for (int i = 0; i < g_api_count; i++) {
        if (info->si_pid == g_apis[i].pid) {
            g_apis[i].last_heartbeat = time(NULL);
            syslog(LOG_INFO, "Fetch Manager - %s heartbeat received", g_apis[i].name);
        }
    }
}

void cleanup(void) {
    closelog();
}