#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/inotify.h>
#include <unistd.h>
#include <string.h>

#define ATOMIC_FILE_RW_IMPLEMENTATION
#include "../libs/atomic_file_rw.h"

#include "compute.h"

#define DB_PATH "tester"

pid_t g_ppid = 0;

void* heartbeat();
int compute_work();

int main(int argc, char* argv[]) {
    if (argc < 1) {
        fprintf(stderr, "Usage: <PPID>\n");
        return EXIT_FAILURE;
    }

    printf("Compute manager started\n");

    // Parse arguments
    char* endptr;
    g_ppid = (int) strtol(argv[1], &endptr, 10);
    if (*endptr != '\0') return EXIT_FAILURE;

    // Initialize inotify
    int inotify_fd = inotify_init();
    if (inotify_fd < 0) {
        perror("inotify_init failed");
        return EXIT_FAILURE;
    }

    // Watch file for modifications
    const char* db_path = DB_PATH;
    int watch_fd = inotify_add_watch(inotify_fd, db_path, IN_CLOSE_WRITE);
    if (watch_fd < 0) {
        perror("inotify_add_watch failed");
        close(inotify_fd);
        return EXIT_FAILURE;
    }

    // Heartbeat
    pthread_t thread_hb;
    pthread_create(&thread_hb, NULL, heartbeat, NULL);
    pthread_detach(thread_hb);

    char buffer[4096];
    while (1) {
        int len = read(inotify_fd, buffer, sizeof(buffer));
        if (len > 0) {
            compute_work();
        }
    }

    return 0;
}

void* heartbeat() {
    while (1) {
        // if (kill(g_ppid, SIGRTMIN) == -1) {
        //     perror("Could not signal daemon, terminating.\n");
        //     exit(EXIT_FAILURE);
        // }
        printf("Beating...\n");
        sleep(1);
    }

    return NULL;
}

int compute_work() {
    printf("Compute working\n");

    // Read from file
    // Calculate
    // Save to file

    return 0;
}