#define _POSIX_C_SOURCE 200809L
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <sys/syslog.h>

#define HEARTBEAT_TIMEOUT 10

typedef struct api {
    char* name;
    char* path;
    int interval;
    pid_t pid;
    time_t last_heartbeat;
} api;

api apis[2];

pid_t g_parent_pid = 0;
int g_hb_speed;

void* heartbeat();
void handle_child_heartbeat(int sig, siginfo_t* info, void* context);
int setup();

int main(int argc, char* argv[]) {
    if (argc < 3) {
        fprintf(stderr, "Usage: ./path/to/bin <PPID>\n");
        return EXIT_FAILURE;
    }
	openlog("SUNSPOTS_FETCH", LOG_PID, LOG_CONS);
    printf("Starting fetch manager.\n");

    // Parse arguments
    char* endptr;
    g_parent_pid = (int)strtol(argv[1], &endptr, 10);
    if (*endptr != '\0') return EXIT_FAILURE;
	g_hb_speed = (int)strtol(argv[2], &endptr, 10);
	if (*endptr != '\0') return EXIT_FAILURE;

    //setenv("SUNSPOTS CONFIG", watch_table[index].config, 1);
    
    setup();

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

        for (int i = 0; i < (int)(sizeof(apis) / sizeof(apis[0])); i++) {
            // Spawn process
            if (apis[i].pid == 0) {
                printf("Spawning %s worker...\n", apis[i].name);
                apis[i].pid = fork();
                if (apis[i].pid == -1) {
                    perror("fork");
                    exit(EXIT_FAILURE);
                } else if (apis[i].pid == 0) {
                    execv(apis[i].path, (char* const[]){apis[i].path, parent_pid_str, NULL});
                    perror("execv");
                    exit(EXIT_FAILURE);
                }
                apis[i].last_heartbeat = time(NULL);
            }

            time_t now = time(NULL);

            // Check for timeouts
            if (apis[i].pid > 0 && now - apis[i].last_heartbeat > HEARTBEAT_TIMEOUT) {
                printf("%s process timeout, killing...\n", apis[i].name);
                kill(apis[i].pid, SIGKILL);
                waitpid(apis[i].pid, NULL, 0);
                apis[i].pid = 0;
            }

            // Check for unexpected exits
            if (apis[i].pid > 0 && waitpid(apis[i].pid, NULL, WNOHANG) > 0) {
                printf("%s process exited undexpectedly\n", apis[i].name);
                apis[i].pid = 0;
            }
        }

        sleep(2);
    }
	closelog();
    return 0;
}

int setup() {
    api* api_curr;
    api_curr = &apis[0];
    api_curr->name = "Openmeteo";
    api_curr->path = "./apis/fetch_openmeteo";
    api_curr->interval = 900;

    api_curr = &apis[1];
    api_curr->name = "Elprisjustnu";
    api_curr->path = "./apis/fetch_elprisjustnu";
    api_curr->interval = 3600 * 24;

    // api_curr = &apis[3];
    // api_curr->name = "SMHI";
    // api_curr->path = "./apis/fetch_smhi";
    // api_curr->interval = 900;


    for (int i = 0; i < (int)(sizeof(apis) / sizeof(apis[0])); i++) {
        printf("API %d: %s\n", i + 1, apis[i].name);
    }  
    return 0;
}

void* heartbeat() {
    while (1) {
        if (kill(g_parent_pid, SIGRTMIN) == -1) {
             perror("Could not signal daemon, terminating.\n");
             exit(EXIT_FAILURE);
        }
        printf("Beating...\n");
        sleep (g_hb_speed);
    }

    return NULL;
}

void handle_child_heartbeat(int sig, siginfo_t* info, void* context) {
    for (int i = 0; i < (int)(sizeof(apis) / sizeof(apis[0])); i++) {
        if (info->si_pid == apis[i].pid) {
            apis[i].last_heartbeat = time(NULL);
            printf("%s heartbeat received\n", apis[i].name);
        }
    }
}
