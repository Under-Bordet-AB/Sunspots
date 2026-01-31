#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "curly.h"
// #include "parsers.h"
// #include "database_manager.h"

#define URL_OPENMETEO ""
#define URL_SMHI ""
#define URL_ELPRIS ""

pid_t g_ppid = 0;
int g_interval = 900;
int g_timeout = 30;

void* heartbeat();
void* fetch_openmeteo_work();
void* fetch_smhi_work();
void* fetch_elpris_work();

int fetch_from_url(char* url, char** buffer);
/*
int normalize_openmeteo(char* raw, char** buffer);
int normalize_smhi(char* raw, char** buffer);
int normalize_elpris(char* raw, char** buffer);
int save_to_database(char* data, char* filename);
*/
int main(int argc, char* argv[]) {
    if (argc < 3) {
        fprintf(stderr, "Usage: ./path/to/bin <PPID> <interval> <timeout>\n");
        return EXIT_FAILURE;
    }

    printf("Starting fetch manager.\n");

    // Parse arguments
    char* endptr;
    g_ppid = (int)strtol(argv[1], &endptr, 10);
    if (*endptr != '\0') return EXIT_FAILURE;

    g_interval = (int) atoi(argv[2]);
    g_timeout = (int) atoi(argv[3]);

    while (1) {
        printf("Fetching from APIs...\n");

        pthread_t thread0, thread1, thread2;

        pthread_create(&thread0, NULL, (void* (*) (void*) ) heartbeat, NULL);
        pthread_detach(thread0);

        pthread_create(&thread1, NULL, (void* (*) (void*) ) fetch_openmeteo_work, NULL);
        pthread_detach(thread1);

        pthread_create(&thread2, NULL, (void* (*) (void*) ) fetch_elpris_work, NULL);
        pthread_detach(thread2);

        sleep(g_interval);
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
        sleep (5);
    }

    return NULL;
}

void* fetch_openmeteo_work() {
    printf("Fetching data from openmeteo.\n");

    char* buffer = NULL;
    if (fetch_from_url(URL_OPENMETEO, &buffer) < 0) {
        printf("Fetching from openmeteo failed\n");
        return NULL;
    }

    if (buffer) {
        printf("%s\n", buffer);
        free(buffer);
    }

    return NULL;
}

void* fetch_smhi_work() {
    printf("Fetching data from smhi.\n");

    char* buffer = NULL;
    if (fetch_from_url(URL_SMHI, &buffer) < 0) {
        printf("Fetching from openmeteo failed\n");
        return NULL;
    }

    if (buffer) {
        printf("%s\n", buffer);
        free(buffer);
    }

    return NULL;
}

void* fetch_elpris_work() {
    printf("Fetching data from elpris.\n");

    char* buffer = NULL;
    if (fetch_from_url(URL_ELPRIS, &buffer) < 0) {
        printf("Fetching from openmeteo failed\n");
        return NULL;
    }

    if (buffer) {
        printf("%s\n", buffer);
        free(buffer);
    }
    return NULL;
}

int fetch_from_url(char* url, char** buffer) {
    printf("Running fetch_from_url function\n");

    curly_t* curly = NULL;
    if (curly_init(&curly) < 0) {
        printf("Curly failed to initiate\n");
        return -1;
    }

    if (curly_make_request(&curly, url) < 0) {
        printf("Request failed\n");
        curly_cleanup(&curly);
        return -1;
    }

    while (curly_poll(&curly) == 0) {
        printf("Polling curly...\n");
        if (curly_is_running(&curly) == 0) {
            printf("Curling complete!\n");
            break;
        }
    }

    char* response = NULL;
    if (curly_read_response(&curly, &response) < 0) {
        printf("Reading response failed\n");
        curly_cleanup(&curly);
        return -1;
    }
    
    if (response) {
        *buffer = response;
    }

    if (curly_cleanup(&curly) < 0) {
        printf("Failed to cleanup curly\n");
        return -1;
    }

    return 0;
}
/*
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