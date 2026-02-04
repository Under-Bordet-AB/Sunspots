#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <signal.h>
#include <pthread.h>

#include "curly.h"

#define API_NAME "HTTPBIN"
#define API_URL "http://httpbin.org/get"
#define INTERVAL 30
#define NORMALIZE_FUNCTION normalize_data()
#define DATABASE_SAVE_FUNCTION save_to_database();

pid_t parent_ppid;

void* heartbeat();
int fetch_from_url(char* url, char** buffer);
int normalize_data(char* raw, char** buffer);
int save_to_database(char* buffer);

int main(int argc, char* argv[]) {
    if (argc < 3) {
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
            return NULL;
        }

        if (!buffer) {
            printf("%s buffer is NULL\n", API_NAME);
            return NULL;
        }

        printf("%s API response: %.100s...\n", API_NAME, buffer);

        char** normalized_data = NULL;
        if (normalize_data(buffer, &normalized_data) < 0) {
            printf("Couldn't normalize %s data.\n", API_NAME);
            free(buffer);
            return NULL;
        }

        if ((save_to_database(normalized_data) < 0)) {
            printf("Couldn't save %s to database.\n");
            free(normalized_data);
            free(buffer);
            return NULL;
        }
        
        free(normalized_data);
        free(buffer);
        
        return NULL;

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

int normalize_data(char* raw, char** buffer) {
    return 0;
}

int save_to_database(char* buffer) {
    if (af_save("test", "test", buffer) < 0) {
        printf("Couldn't save %s data to database.\n", API_NAME);
    }
    return 0;
}