#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

pid_t g_ppid = 0;

void* heartbeat();

int main(int argc, char* argv[]) {
    if (argc < 1) {
        fprintf(stderr, "Usage: ...\n");
        return EXIT_FAILURE;
    }

    printf("Compute manager started\n");

    // Parse arguments
    char* endptr;
    g_ppid = (int) strtol(argv[1], &endptr, 10);
    if (*endptr != '\0')
        return EXIT_FAILURE;

    pthread_t thread_hb;
    pthread_create(&thread_hb, NULL, heartbeat, NULL);
    pthread_detach(thread_hb);

    while (1) {
        // EPOLL (database file)
        sleep(5);
    }

    return 0;
}

void* heartbeat() {
    while (1) {
        if (kill(g_ppid, SIGRTMIN) == -1) {
            perror("Could not signal daemon, terminating.\n");
            exit(EXIT_FAILURE);
        }
        printf("Beating...\n");
        sleep(1);
    }

    return NULL;
}