#ifndef FETCH_MANAGER_H
#define FETCH_MANAGER_H

#include <signal.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

// #include "curl_client.h"
// #include "parser_smhi.h"
// #include "parser_elpris.h"
// #include "database_manager.h"

static pid_t fetch_pid = -1;

void fetch_cleanup() {
    printf("Cleaning up fetch manager.\n");

    if (fetch_pid > 0) {
        kill(fetch_pid, SIGTERM);
        waitpid(fetch_pid, NULL, 0);
    }
}

void fetch_start() {
    printf("Starting fetch manager.\n");

    int iterations = 0;
    int max_iterations = 5;

    while (iterations < max_iterations) {
        printf("RUNNING!!! Iteration %d/%d\n", iterations + 1, max_iterations);

        iterations++;

        if (iterations < max_iterations) {
            sleep(1);
        }
    }
}

void fetch_init() {
    printf("Initializing fetch manager.\n");

    fetch_pid = fork();

    if (fetch_pid < 0) {
        perror("Fork failed");
        return;
    } else if (fetch_pid == 0) {
        fetch_start();
        _exit(0);
    }
}

int fetch_from_smhi() {
    // curl from site
    // normalize with parser
    // save to database
    return 0;
}

int fetch_from_elpris() {
    // curl from site
    // normalize with parser
    // save to database
    return 0;
}

#endif