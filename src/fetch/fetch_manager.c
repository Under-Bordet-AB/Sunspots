#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

// #include "curly.h"
// #include "parsers.h"
// #include "database_manager.h"

#define URL_OPENMETEO ""
#define URL_SMHI ""
#define URL_ELPRIS ""

int g_interval = 0;
int g_timeout = 0;

void* fetch_openmeteo_work();
void* fetch_smhi_work();
void* fetch_elpris_work();
/*
int fetch_from_url(char* url, char** buffer);
int normalize_openmeteo(char* raw, char** buffer);
int normalize_smhi(char* raw, char** buffer);
int normalize_elpris(char* raw, char** buffer);
int save_to_database(char* data, char* filename);
*/
int main(int argc, char* argv[]) {
    if (argc < 3) {
        fprintf(stderr, "Usage: ./path/to/bin <interval> <timeout>\n");
        return EXIT_FAILURE;
    }

    printf("Starting fetch manager.\n");

    /*
    char* endptr;
    pid_t ppid = (int)strtol(argv[1], &endptr, 10);
    if (*endptr != '\0') return EXIT_FAILURE;

    if (kill(ppid, SIGRTMIN) == -1) {
        perror("Could not signal daemon, terminating.\n");
        exit(EXIT_FAILURE);
    }
    */

    // Parse arguments
    g_interval = (int) atoi(argv[1]);
    g_timeout = (int) atoi(argv[2]);

    while (1) {
        printf("Fetching from APIs...\n");

        pthread_t thread1, thread2;

        pthread_create(&thread1, NULL, (void* (*) (void*) ) fetch_openmeteo_work, NULL);
        pthread_detach(thread1);

        pthread_create(&thread2, NULL, (void* (*) (void*) ) fetch_elpris_work, NULL);
        pthread_detach(thread2);

        sleep(g_interval);
    }

    return 0;
}

void* fetch_openmeteo_work() {
    printf("Fetching data from openmeteo.\n");
    return NULL;
}

void* fetch_smhi_work() {
    printf("Fetching data from smhi.\n");
    return NULL;
}

void* fetch_elpris_work() {
    printf("Fetching data from elpris.\n");
    return NULL;
}
/*
int fetch_from_url(char* url, char** buffer) {
    return 0;
}

int normalize_openmeteo(char* raw, char** buffer) {
    return 0;
}

int normalize_smhi(char* raw, char** buffer) {
    return 0;
}

int normalize_elpris(char* raw, char** buffer) {
    return 0;
}

int save_to_database(char* data, char* filename) {
    return 0;
}
*/