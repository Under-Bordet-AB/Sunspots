#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "curly.h"
// #include "parsers.h"

#define ATOMIC_FILE_RW_IMPLEMENTATION
#include "../libs/atomic_file_rw.h"

#define URL_OPENMETEO "https://api.open-meteo.com/v1/forecast?latitude=59.3293&longitude=18.0686&current=temperature_2m,wind_speed_10m&hourly=temperature_2m,relative_humidity_2m,wind_speed_10m"
#define URL_SMHI "https://opendata.smhi.se/metfcst/snow1gv1/get_point_forecast?latitude=59.3293&longitude=18.0686"
#define URL_ELPRISJUSTNU "https://www.elprisetjustnu.se/api/v1/prices/%04d/%02d-%02d_SE3.json"

pid_t g_ppid = 0;
int g_interval = 900;

// Fundamental functions
void* heartbeat();
void* fetch_openmeteo_work();
void* fetch_smhi_work();
void* fetch_elprisjustnu_work();

// Helper functions
int fetch_from_url(char* url, char** buffer);
int normalize_openmeteo(char* raw, char** buffer);
int normalize_smhi(char* raw, char** buffer);
int normalize_elpris(char* raw, char** buffer);

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

    g_interval = (int)strtol(argv[2], &endptr, 10);
    if (*endptr != '\0') return EXIT_FAILURE;

    pthread_t thread0;
    pthread_create(&thread0, NULL, (void* (*) (void*) ) heartbeat, NULL);
    pthread_detach(thread0);

    while (1) {
        printf("Fetching from APIs...\n");

        pthread_t thread1, thread2;

        pthread_create(&thread1, NULL, (void* (*) (void*) ) fetch_openmeteo_work, NULL);
        pthread_detach(thread1);

        pthread_create(&thread2, NULL, (void* (*) (void*) ) fetch_elprisjustnu_work, NULL);
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
        printf("Couldn't fetch from Openmeteo.\n");
        return NULL;
    }

    if (!buffer) {
        printf("Openmeteo buffer is NULL\n");
        return NULL;
    }

    printf("Openmeteo API response: %.100s...\n", buffer);

    // normalize_openmeteo
    
    if (af_save("test", "test", buffer) < 0) {
        printf("Couldn't save Openmeteo data to database.\n");
    }
    
    free(buffer);
    
    return NULL;
}

void* fetch_smhi_work() {
    char* buffer = NULL;
    if (fetch_from_url(URL_SMHI, &buffer) < 0) {
        printf("Couldn't fetch from SMHI.\n");
        return NULL;
    }

    if (!buffer) {
        printf("SMHI buffer is NULL\n");
        return NULL;
    }

    printf("SMHI API response: %.100s...\n", buffer);

    // normalize_smhi

    if (af_save("test", "test", buffer) < 0) {
        printf("Couldn't save SMHI data to database.\n");
    }
    
    free(buffer);

    return NULL;
}

void* fetch_elprisjustnu_work() {
    char* buffer = NULL;
    
    char url[128];
    time_t now = time(NULL);
    struct tm *t = localtime(&now);

    snprintf(url, sizeof(url), URL_ELPRISJUSTNU, t->tm_year + 1900, t->tm_mon + 1, t->tm_mday);

    printf("%s\n", url);
    
    if (fetch_from_url(url, &buffer) < 0) {
        printf("Couldn't fetch from Elprisjustnu.\n");
        return NULL;
    }

    if (!buffer) {
        printf("Elprisjustnu buffer is NULL\n");
        return NULL;
    }

    printf("Elprisjustnu API response: %.100s...\n", buffer);

    // normalize_elprisjustnu

    if (af_save("test", "test", buffer) < 0) {
        printf("Couldn't save Elprisjustnu data to database.\n");
    }
    
    free(buffer);

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

int normalize_openmeteo(char* raw, char** buffer) {
    return 0;
}

int normalize_smhi(char* raw, char** buffer) {
    return 0;
}

int normalize_elprisjustnu(char* raw, char** buffer) {
    return 0;
}
