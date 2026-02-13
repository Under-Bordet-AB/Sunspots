#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <syslog.h>
#include <signal.h>
#include <pthread.h>
#include <time.h>

#include "../fetch_utils.h"
#include "../../libs/curly.h"
#include "../../databases/database_provider.h"

#define API_NAME "Elprisjustnu"
#define API_URL "https://www.elprisetjustnu.se/api/v1/prices/%04d/%02d-%02d_SE3.json"
#define INTERVAL 3600*24

pid_t parent_ppid;

void* heartbeat();
int normalize_data(char* raw, char** buffer);
int save_to_database(char* buffer);
void cleanup(void);

int main(int argc, char* argv[]) {
    atexit(cleanup);

    openlog("SUNSPOTS_FETCH_ELPRISJUSTNU", LOG_PID, LOG_DAEMON);
    
    if (argc < 2) {
        syslog(LOG_ERR, "Fetch API - Elprisjustnu - Usage: ./path/to/bin <PPID>");
        exit(EXIT_FAILURE);
    }

    syslog(LOG_INFO, "Fetch API - Elprisjustnu - Starting...");

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

        char url[128];
        time_t now = time(NULL);
        struct tm *t = localtime(&now);

        snprintf(url, sizeof(url), API_URL, t->tm_year + 1900, t->tm_mon + 1, t->tm_mday);

        if (fetch_from_url(url, &buffer) < 0) {
            syslog(LOG_WARNING, "Fetch API - Elprisjustnu - Couldn't fetch from API.");
            exit(EXIT_FAILURE);
        }

        if (!buffer) {
            syslog(LOG_WARNING, "Fetch API - Elprisjustnu - Buffer is NULL.");
            exit(EXIT_FAILURE);
        }

        char* normalized_data = NULL;
        if (normalize_data(buffer, &normalized_data) < 0) {
            syslog(LOG_WARNING, "Fetch API - Elprisjustnu - Couldn't normalize data.");
            free(buffer);
            exit(EXIT_FAILURE);
        }

        if ((save_to_database(buffer) < 0)) {
            syslog(LOG_WARNING, "Fetch API - Elprisjustnu - Couldn't save data to database.");
            free(normalized_data);
            free(buffer);
            exit(EXIT_FAILURE);
        }

        syslog(LOG_INFO, "Fetch API - Elprisjustnu - Data successfully normalized and saved!");
        
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
            syslog(LOG_ERR, "Fetch API - Elprisjustnu - Couldn't signal daemon, terminating.");
            exit(EXIT_FAILURE);
        }
        syslog(LOG_INFO, "Fetch API - Elprisjustnu - Beating...");
        sleep(5);
    }

    return NULL;
}

int normalize_data(char* raw, char** buffer) {
    return 0;
}

int save_to_database(char* buffer) {
    const i_database* database = get_database(DB_MOCK);
    if (database == NULL) {
        return -1;
    }

    // if (database->save_elpris_24h() != DATABASE_OK) {
    //     return -1;
    // }

    return 0;
}

void cleanup(void) {
    closelog();
}
