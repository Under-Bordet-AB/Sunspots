#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "curly.h"
// #include "parsers.h"

#define ATOMIC_FILE_RW_IMPLEMENTATION
#include "atomic_file_rw.h"

#define URL_OPENMETEO ""
#define URL_SMHI ""
#define URL_ELPRIS ""

pid_t g_ppid = 0;
int g_interval = 900;

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
        fprintf(stderr, "Usage: ./path/to/bin <PPID> <interval>\n");
        return EXIT_FAILURE;
    }

    printf("Starting fetch manager.\n");

    // Parse arguments
    char* endptr;
    g_ppid = (int)strtol(argv[1], &endptr, 10);
    if (*endptr != '\0') return EXIT_FAILURE;

    g_interval = (int) atoi(argv[2]);

    pthread_t thread0;
    pthread_create(&thread0, NULL, (void* (*) (void*) ) heartbeat, NULL);
    pthread_detach(thread0);

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

void* heartbeat() {
    while (1) {
        // if (kill(g_ppid, SIGRTMIN) == -1) {
        //     perror("Could not signal daemon, terminating.\n");
        //     exit(EXIT_FAILURE);
        // }
        printf("Beating...\n");
        sleep (1);
    }

    return NULL;
}

void* fetch_openmeteo_work() {
    char* buffer = NULL;
    if (fetch_from_url(URL_OPENMETEO, &buffer) < 0) {
        return NULL;
    }

    if (buffer) {
        printf("%s\n", buffer);
        free(buffer);
    }

    return NULL;
}

void* fetch_smhi_work() {
    char* buffer = NULL;
    if (fetch_from_url(URL_SMHI, &buffer) < 0) {
        return NULL;
    }

    if (buffer) {
        printf("%s\n", buffer);
        free(buffer);
    }

    return NULL;
}

void* fetch_elpris_work() {
    char* buffer = NULL;
    if (fetch_from_url(URL_ELPRIS, &buffer) < 0) {
        return NULL;
    }

    if (buffer) {
        printf("%s\n", buffer);
        free(buffer);
    }

    return NULL;
}

int fetch_from_url(char* url, char** buffer) {
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

    int iterations = 0;
    int max_iterations = 300; // 30 seconds

    while (curly_poll(&curly) == 0) {
        if (curly_is_running(&curly) == 0) {
            break;
        }

        iterations++;
        if (iterations > max_iterations) {
            break;
        }

        usleep(100000); // 10 milliseconds
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