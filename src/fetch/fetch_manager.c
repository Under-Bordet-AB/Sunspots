#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

// #include "curl_client.h"
// #include "parsers.h"
// #include "database_manager.h"

#define URL_OPENMETEO ""
#define URL_SMHI ""
#define URL_ELPRIS ""

int g_interval = 0;
int g_timeout = 0;

int fetch_openmeteo_work();
int fetch_smhi_work();
int fetch_elpris_work();

int main(int argc, char* argv[]) {
    if (argc < 3) {
        fprintf(stderr, "Usage: ./path/to/bin <interval> <timeout>\n");
        return EXIT_FAILURE;
    }

    printf("Starting fetch manager.\n");

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

int fetch_openmeteo_work() {
    printf("Fetching data from openmeteo.\n");
    return 0;
}

int fetch_smhi_work() {
    printf("Fetching data from smhi.\n");
    return 0;
}

int fetch_elpris_work() {
    printf("Fetching data from elpris.\n");
    return 0;
}

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