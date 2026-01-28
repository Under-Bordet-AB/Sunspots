#include "fetch/fetch_manager.h"

static pid_t fetch_pid = -1;

int main(int argc, char* argv[]) {
    if (argc < 3) {
        fprintf(stderr, "Usage: %s <interval> <timeout>\n", argc > 0 ? argv[0] : "fetch_manager");
        return EXIT_FAILURE;
    }

    printf("Initializing fetch manager.\n");

    int interval = (int) atoi(argv[1]);
    int timeout = (int) atoi(argv[2]);

    fetch_pid = fork();

    if (fetch_pid < 0) {
        perror("Fork failed");
        return -1;
    } else if (fetch_pid == 0) {
        fetch_work(interval, timeout);
        _exit(0);
    }

    atexit(fetch_dispose);

    if (fetch_pid > 0) {
        int status;
        waitpid(fetch_pid, &status, 0);
    }

    return 0;
}

void fetch_work(int interval, int timeout) {
    printf("Starting fetch manager.\n");

    int iterations = 0;
    int max_iterations = 5;

    while (iterations < max_iterations) {
        printf("RUNNING!!! Iteration %d/%d\n", iterations + 1, max_iterations);

        iterations++;

        if (iterations < max_iterations) {
            // sleep(interval);
            usleep(500000);
        }
    }
}

void fetch_dispose() {
    printf("Cleaning up fetch manager.\n");

    if (fetch_pid > 0) {
        kill(fetch_pid, SIGTERM);
        waitpid(fetch_pid, NULL, 0);
    }
}