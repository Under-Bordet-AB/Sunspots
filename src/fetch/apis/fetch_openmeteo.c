#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <syslog.h>
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
void cleanup(void);

int main(int argc, char* argv[]) {
    atexit(cleanup);

    openlog("SUNSPOTS_FETCH_OPENMETEO", LOG_PID | LOG_CONS, LOG_DAEMON);
    
    if (argc < 2) {
        syslog(LOG_ERR, "Fetch API - Openmeteo - Usage: ./path/to/bin <PPID>");
        exit(EXIT_FAILURE);
    }

    syslog(LOG_INFO, "Fetch API - Openmeteo - Starting...");

    char* endptr;
    parent_ppid = (int)strtol(argv[1], &endptr, 10);
    if (*endptr != '\0') {
        exit(EXIT_FAILURE);
    }

    pthread_t thread_heartbeat;
    pthread_create(&thread_heartbeat, NULL, (void* (*) (void*)) heartbeat, NULL);
    pthread_detach(thread_heartbeat);

    while (1) {
        char* buffer = NULL;
        if (fetch_from_url(API_URL, &buffer) < 0) {
            syslog(LOG_WARNING, "Fetch API - Openmeteo - Couldn't fetch from API");
            exit(EXIT_FAILURE);
        }

        if (!buffer) {
            syslog(LOG_WARNING, "Fetch API - Openmeteo - Buffer is NULL");
            exit(EXIT_FAILURE);
        }

        char* normalized_data = NULL;
        if (normalize_data(buffer, &normalized_data) < 0) {
            syslog(LOG_WARNING, "Fetch API - Openmeteo - Couldn't normalize data.");
            free(buffer);
            exit(EXIT_FAILURE);
        }

        if ((save_to_database(buffer) < 0)) {
            syslog(LOG_WARNING, "Fetch API - Openmeteo - Couldn't save data to database");
            free(normalized_data);
            free(buffer);
            exit(EXIT_FAILURE);
        }
        
        if (normalized_data != NULL) free(normalized_data);
        if (buffer != NULL) free(buffer);
        
        sleep(INTERVAL);
    }

    closelog();

    return 0;
}

void* heartbeat() {
    while (1) {
        if (kill(parent_ppid, SIGRTMIN) == -1) {
            syslog(LOG_ERR, "Fetch API - Openmeteo - Couldn't signal daemon, terminating.");
            exit(EXIT_FAILURE);
        }
        syslog(LOG_INFO, "Fetch API - Openmeteo - Beating...");
        sleep(5);
    }

    return NULL;
}

int normalize_data(char* raw, char** buffer) {
    return 0;
}

int save_to_database(char* buffer) {
    if (af_save("Openmeteo", "test", buffer) < 0) {
        return -1;
    }

    return 0;
}

void cleanup(void) {
    closelog();
}