#define _POSIX_C_SOURCE 200809L
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define HEARTBEAT_TIMEOUT 5

pid_t g_parent_pid = 0;

pid_t g_openmeteo_pid = 0;
pid_t g_smhi_pid = 0;
pid_t g_elprisjustnu_pid = 0;

time_t g_last_openmeteo_heartbeat = 0;
time_t g_last_smhi_heartbeat = 0;
time_t g_last_elprisjustnu_heartbeat = 0;

void* heartbeat();
void handle_child_heartbeat(int sig, siginfo_t* info, void* context);

int main(int argc, char* argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: ./path/to/bin <PPID>\n");
        return EXIT_FAILURE;
    }

    printf("Starting fetch manager.\n");

    // Parse arguments
    char* endptr;
    g_parent_pid = (int)strtol(argv[1], &endptr, 10);
    if (*endptr != '\0') return EXIT_FAILURE;

    // Set up signal handler for child heartbeats
    struct sigaction sa;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_SIGINFO;
    sa.sa_sigaction = handle_child_heartbeat;
    sigaction(SIGRTMIN, &sa, NULL);

    pthread_t thread_heartbeat;
    pthread_create(&thread_heartbeat, NULL, (void* (*) (void*)) heartbeat, NULL);
    pthread_detach(thread_heartbeat);

    g_openmeteo_pid = 0;
    g_elprisjustnu_pid = 0;
    g_last_openmeteo_heartbeat = time(NULL);
    g_last_elprisjustnu_heartbeat = time(NULL);

    while (1) {
        char parent_pid_str[16];
        snprintf(parent_pid_str, sizeof(parent_pid_str), "%d", getpid());

        // Spawn processes
        if (g_openmeteo_pid == 0) {
            printf("Spawning Openmeteo worker...\n");
            g_openmeteo_pid = fork();
            if (g_openmeteo_pid == -1) {
                perror("fork");
                return EXIT_FAILURE;
            } else if (g_openmeteo_pid == 0) {
                execv("./fetch_openmeteo", (char* const[]){"./fetch_openmeteo", parent_pid_str, NULL});
                perror("execv");
                exit(EXIT_FAILURE);
            }
            g_last_openmeteo_heartbeat = time(NULL);
        }

        if (g_elprisjustnu_pid == 0) {
            printf("Spawning Elprisjustnu worker...\n");
            g_elprisjustnu_pid = fork();
            if (g_elprisjustnu_pid == -1) {
                perror("fork");
                return EXIT_FAILURE;
            } else if (g_elprisjustnu_pid == 0) {
                execv("./fetch_elprisjustnu", (char* const[]){"./fetch_elprisjustnu", parent_pid_str, NULL});
                perror("execv");
                exit(EXIT_FAILURE);
            }
            g_last_elprisjustnu_heartbeat = time(NULL);
        }

        time_t now = time(NULL);

        // Check for timeouts
        if (g_openmeteo_pid > 0 && now - g_last_openmeteo_heartbeat > HEARTBEAT_TIMEOUT) {
            printf("Openmeteo process timeout, killing...\n");
            kill(g_openmeteo_pid, SIGKILL);
            waitpid(g_openmeteo_pid, NULL, 0);
            g_openmeteo_pid = 0;
        }

        if (g_elprisjustnu_pid > 0 && now - g_last_elprisjustnu_heartbeat > HEARTBEAT_TIMEOUT) {
            printf("Elprisjustnu process timeout, killing...\n");
            kill(g_elprisjustnu_pid, SIGKILL);
            waitpid(g_elprisjustnu_pid, NULL, 0);
            g_elprisjustnu_pid = 0;
        }

        // Handle unexpected exits
        if (g_openmeteo_pid > 0 && waitpid(g_openmeteo_pid, NULL, WNOHANG) > 0) {
            printf("Openmeteo process exited unexpectedly\n");
            g_openmeteo_pid = 0;
        }

        if (g_elprisjustnu_pid > 0 && waitpid(g_elprisjustnu_pid, NULL, WNOHANG) > 0) {
            printf("Elprisjustnu process exited unexpectedly\n");
            g_elprisjustnu_pid = 0;
        }

        sleep(2);
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
        sleep (1);
    }

    return NULL;
}

void handle_child_heartbeat(int sig, siginfo_t* info, void* context) {
    if (info->si_pid == g_openmeteo_pid) {
        g_last_openmeteo_heartbeat = time(NULL);
        printf("Openmeteo heartbeat received\n");
    } else if (info->si_pid == g_elprisjustnu_pid) {
        g_last_elprisjustnu_heartbeat = time(NULL);
        printf("Elprisjustnu heartbeat received\n");
    }
}