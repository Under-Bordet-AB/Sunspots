#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/inotify.h>
#include <syslog.h>
#include <unistd.h>
#include <string.h>

#define ATOMIC_FILE_RW_IMPLEMENTATION
#include "../libs/atomic_file_rw.h"

#include "compute.h"

#define DB_PATH "tester"

pid_t g_ppid = 0;
int g_heartbeat_freq;

void* heartbeat();
int compute_work();
void cleanup(void);

int main(int argc, char* argv[]) {
    atexit(cleanup);

    openlog("SUNSPOTS_COMPUTE_MANAGER", LOG_PID | LOG_CONS, LOG_DAEMON);

    if (argc < 3) {
        fprintf(stderr, "Usage: <PPID> <Heartbeat frequency in seconds>\n");
        exit(EXIT_FAILURE);
    }

    syslog(LOG_INFO, "Compute Manager - Starting...");

    // Parse arguments
    char* endptr;
    g_ppid = (int)strtol(argv[1], &endptr, 10);
    if (*endptr != '\0') exit(EXIT_FAILURE);
    g_heartbeat_freq = (int)strtol(argv[2], &endptr, 10);
    if (*endptr != '\0') exit(EXIT_FAILURE);

    // Initialize inotify
    int inotify_fd = inotify_init();
    if (inotify_fd < 0) {
        syslog(LOG_ERR, "Compute Manager - inotify_init failed.");
        exit(EXIT_FAILURE);
    }

    // Watch file for modifications
    const char* db_path = DB_PATH;
    int watch_fd = inotify_add_watch(inotify_fd, db_path, IN_CLOSE_WRITE);
    if (watch_fd < 0) {
        syslog(LOG_ERR, "Compute Manager - inotify_add_watch failed.");
        close(inotify_fd);
        exit(EXIT_FAILURE);
    }

    // Heartbeat
    pthread_t thread_hb;
    pthread_create(&thread_hb, NULL, heartbeat, NULL);
    pthread_detach(thread_hb);

    char buffer[4096];
    while (1) {
        int len = read(inotify_fd, buffer, sizeof(buffer));
        if (len > 0) {
            if (compute_work() == 0) {
                syslog(LOG_INFO, "Compute Manager - Successfully computed result!");
            } else {
                syslog(LOG_WARNING, "Compute Manager - Failed to compute results.");
            }
        }
    }

    exit(EXIT_FAILURE);
}

void* heartbeat() {
    while (1) {
        // if (kill(g_ppid, SIGRTMIN) == -1) {
        //     perror("Could not signal daemon, terminating.\n");
        //     exit(EXIT_FAILURE);
        // }
        printf("Beating...\n");
        sleep(1);
    }

    return NULL;
}

int load_data(data_t** data) {
    *data = malloc(sizeof(data_t));
    int type = 1;

    switch (type) {
        case 0: // Database
        syslog(LOG_INFO, "Loading database data...");
        break;
        case 1: // Mock
        syslog(LOG_INFO, "Loading mock data...");
        (*data)->irradiance = 0.9;
        (*data)->cloudiness = 0.5;
        (*data)->temperature = 0.5;

        (*data)->spot_price = 1.0;

        (*data)->battery_charge = 0.2;
        break;
    }

    return 0;
}

int save_result(result_t* result) {
    int type = 1;

    switch (type) {
        case 0: // Database
        (void)result;
        //af_save("Compute", "Result", result);
        break;
        case 1: // Log
        syslog(LOG_INFO, "Saving mock result.");
        break;
    }

    return 0;
}

int compute_work() {
    data_t* data;
    load_data(&data);

    result_t* result;

    int calculation_variant = 0;
    switch (calculation_variant) {
        case 0:
        calculate_simple(data, &result);
        break;
        case 1:
        //calculate_linear(data, &result);
        break;
    }
    free(data);

    save_result(result);

    printf("\nResult:\n");
    printf("Buy electricity: %d\n", result->buy_electricity);
    printf("Use solar: %d\n", result->use_solar);
    printf("Charge battery: %d\n", result->charge_battery);
    printf("Sell excess: %d\n\n", result->sell_excess);

    free(result);

    return 0;
}

void cleanup(void) {
    syslog(LOG_INFO, "Compute Manager - Terminating.");
    closelog();
}