#define _POSIX_C_SOURCE 200809L
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <sys/types.h>
#include <sys/wait.h>
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

int g_api_count;
api g_apis[MAX_APIS];

pid_t g_parent_pid = 0;
int g_hearbeat_speed;

void* heartbeat();
void handle_child_heartbeat(int sig, siginfo_t* info, void* context);
int load_apis_from_json(const char* path);

int main(int argc, char* argv[]) {
    if (argc < 3) {
        fprintf(stderr, "Usage: ./path/to/bin <PPID>\n");
        return EXIT_FAILURE;
    }

    printf("Starting fetch manager.\n");

    // Parse arguments
    char* endptr;
    g_parent_pid = (int)strtol(argv[1], &endptr, 10);
    if (*endptr != '\0') return EXIT_FAILURE;
    g_hearbeat_speed = (int)strtol(argv[2], &endptr, 10);
    if (*endptr != '\0') return EXIT_FAILURE;

    // setup();
    load_apis_from_json("fetch_manager_config.json");

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

        for (int i = 0; i < (int)(sizeof(g_apis) / sizeof(g_apis[0])); i++) {
            if (g_apis[i].path == NULL) continue;

            // Spawn process
            if (g_apis[i].pid == 0) {
                printf("Spawning %s worker...\n", g_apis[i].name);
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
                printf("%s process timeout, killing...\n", g_apis[i].name);
                kill(g_apis[i].pid, SIGKILL);
                waitpid(g_apis[i].pid, NULL, 0);
                g_apis[i].pid = 0;
            }

            // Check for unexpected exits
            if (g_apis[i].pid > 0 && waitpid(g_apis[i].pid, NULL, WNOHANG) > 0) {
                printf("%s process exited undexpectedly\n", g_apis[i].name);
                g_apis[i].pid = 0;
            }
        }

        sleep(2);
    }

    return 0;
}

int load_apis_from_json(const char* path) {
    char* json = read_file_to_string(path);
    if (!json) {
        printf("Fetch manager - JSON config could not be loaded\n");
        return -1;
    }

    cJSON* root = cJSON_Parse(json);
    free(json);
    if (!root) {
        printf("Fetch manager - JSON config could not be parsed\n");
        return -1;
    }

    cJSON* apis_json = cJSON_GetObjectItemCaseSensitive(root, "apis");
    if (!cJSON_IsArray(apis_json)) {
        printf("Fetch manager - Could not parse JSON root object\n");
        cJSON_Delete(root);
        return -1;
    }

    int count = cJSON_GetArraySize(apis_json);
    if (count > MAX_APIS) count = MAX_APIS;

    for (int i = 0; i < count; i++) {
        cJSON* item = cJSON_GetArrayItem(apis_json, i);
        cJSON* name = cJSON_GetObjectItemCaseSensitive(item, "name");
        cJSON* bin = cJSON_GetObjectItemCaseSensitive(item, "bin_path");
        cJSON* interval = cJSON_GetObjectItemCaseSensitive(item, "interval");

        if (!cJSON_IsString(name) || !cJSON_IsString(bin) || !cJSON_IsNumber(interval)) {
            continue;
        }

        g_apis[i].name = strdup(name->valuestring);
        g_apis[i].path = strdup(bin->valuestring);
        g_apis[i].interval = interval->valueint;
        g_apis[i].pid = 0;
        g_apis[i].last_heartbeat = time(NULL);
    }

    return 0;
}

void* heartbeat() {
    while (1) {
        // if (kill(g_parent_pid, SIGRTMIN) == -1) {
        //     perror("Could not signal daemon, terminating.\n");
        //     exit(EXIT_FAILURE);
        // }
        printf("Beating...\n");
        sleep(g_hearbeat_speed);
    }

    return NULL;
}

void handle_child_heartbeat(int sig, siginfo_t* info, void* context) {
    for (int i = 0; i < (int)(sizeof(g_apis) / sizeof(g_apis[0])); i++) {
        if (info->si_pid == g_apis[i].pid) {
            g_apis[i].last_heartbeat = time(NULL);
            printf("%s heartbeat received\n", g_apis[i].name);
        }
    }
}
