#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <signal.h>
#include <pthread.h>

#include "fetch_utils.h"
#include "curly.h"
#define ATOMIC_FILE_RW_IMPLEMENTATION
#include "atomic_file_rw.h"

#define API_NAME "Openmeteo"
#define API_URL "https://api.open-meteo.com/v1/forecast?latitude=59.3293&longitude=18.0686&current=temperature_2m,wind_speed_10m&hourly=temperature_2m,relative_humidity_2m,wind_speed_10m"
#define INTERVAL 900

pid_t parent_ppid;

void* heartbeat();
int normalize_data(char* raw, char** buffer);
int save_to_database(char* buffer);

int main(int argc, char* argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: ./path/to/bin <PPID>\n");
        return EXIT_FAILURE;
    }

    printf("Starting %s fetcher.\n", API_NAME);

    char* endptr;
    parent_ppid = (int)strtol(argv[1], &endptr, 10);
    if (*endptr != '\0') return EXIT_FAILURE;

    pthread_t thread_heartbeat;
    pthread_create(&thread_heartbeat, NULL, (void* (*) (void*)) heartbeat, NULL);
    pthread_detach(thread_heartbeat);

    while (1) {
        char* buffer = NULL;
        if (fetch_from_url(API_URL, &buffer) < 0) {
            printf("Couldn't fetch from %s.\n", API_NAME);
            return EXIT_FAILURE;
        }

        if (!buffer) {
            printf("%s buffer is NULL\n", API_NAME);
            return EXIT_FAILURE;
        }

        printf("%s API response: %.100s...\n", API_NAME, buffer);

        char* normalized_data = NULL;
        if (normalize_data(buffer, &normalized_data) < 0) {
            printf("Couldn't normalize %s data.\n", API_NAME);
            free(buffer);
            return EXIT_FAILURE;
        }

        if ((save_to_database(buffer) < 0)) {
            printf("Couldn't save %s data to database.\n", API_NAME);
            free(normalized_data);
            free(buffer);
            return EXIT_FAILURE;
        }
        
        if (normalized_data != NULL) free(normalized_data);
        if (buffer != NULL) free(buffer);
        
        sleep(INTERVAL);
    }

    return 0;
}

void* heartbeat() {
    while (1) {
        if (kill(parent_ppid, SIGRTMIN) == -1) {
            perror("Could not signal daemon, terminating.\n");
            exit(EXIT_FAILURE);
        }
        printf("Beating...\n");
        sleep(5);
    }

    return NULL;
}

int normalize_data(char* raw, char** buffer) {
    return 0;
}

int save_to_database(char* buffer) {
    if (af_save("Openmeteo", "test", buffer) < 0) {
        printf("Couldn't save %s data to database.\n", API_NAME);
    }

    return 0;
}