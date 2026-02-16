#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <signal.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/inotify.h>
#include <syslog.h>
#include <unistd.h>
#include <string.h>

#include "algorithms/compute_simple.h"

#define DUMMY_TRIGGER "DUMMY_TRIGGER"
#define DB_PATH "db_path" // Not implemented yet

pid_t g_ppid = 0;
int g_heartbeat_freq;

void* heartbeat();
int inotify_watch();
int compute_work();
void cleanup(void);

int main(int argc, char* argv[]) {
    atexit(cleanup);

    openlog("SUNSPOTS_COMPUTE_MANAGER", LOG_PID, LOG_DAEMON);

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

    // Inotify watcher
    pthread_t thread_inotify_watch;
    pthread_create(&thread_inotify_watch, NULL, inotify_watch, NULL);
    pthread_detach(thread_inotify_watch);

    while (1) {
        if (kill(g_ppid, SIGRTMIN) == -1) {
            perror("Could not signal daemon, terminating.\n");
            exit(EXIT_FAILURE);
        }
        syslog(LOG_INFO, "Compute Manager - Beating...");
        sleep(g_heartbeat_freq);
    }

    exit(EXIT_FAILURE);
}

int load_data(data_t* data) {
    int type = 1;

    switch (type) {
        case 0: // Database
        break;
        case 1: // Mock
        data->irradiance = 650.0;
        data->cloudiness = 0.2;
        data->temperature = 12.0;
        data->spot_price = 0.9;
        data->battery_charge = 45.0;
        break;
    }

    return 0;
}

int save_result(result_t* result) {
    (void)result;
    int type = 1;

    switch (type) {
        case 0: // Database
        //af_save("Compute", "Result", result);
        break;
        case 1: // Mock
        break;
    }
    
    return 0;
}

int inotify_watch() {
    int inotify_fd = inotify_init();
    if (inotify_fd < 0) {
        syslog(LOG_ERR, "Compute Manager - inotify_init failed.");
        exit(EXIT_FAILURE);
    }

    const char* db_path = DB_PATH;
    int watch_fd = inotify_add_watch(inotify_fd, db_path, IN_CLOSE_WRITE);
    if (watch_fd < 0) {
        syslog(LOG_ERR, "Compute Manager - inotify_add_watch failed.");
        close(inotify_fd);
        exit(EXIT_FAILURE);
    }

    char buffer[4096];
    while (1) {
        int len = read(inotify_fd, buffer, sizeof(buffer));
        if (len == -1) {
            if (errno == EINTR) {
                continue;
            }
            syslog(LOG_ERR, "Compute Manager - Error receiving inotify event.");
            break;
        }

        if (len == 0) {
            syslog(LOG_ERR, "Compute Manager - inotify fd closed (EOF)");
            break;
        }

        if (compute_work() == 0) {
            syslog(LOG_INFO, "Compute Manager - Successfully computed result!");
        } else {
            syslog(LOG_WARNING, "Compute Manager - Failed to compute results.");
        }
    }
}

int compute_work() {
    data_t* data = NULL;
    if (data_init(&data) < 0) {
        syslog(LOG_WARNING, "Compute Manager - Data struct failed to initialize.");
        return -1;
    }

    if (load_data(data) < 0) {
        syslog(LOG_WARNING, "Compute Manager - Data failed to load.");
        data_dispose(&data);
        return -1;
    }

    result_t* result = NULL;
    if (result_init(&result) < 0) {
        syslog(LOG_WARNING, "Compute Manager - Result struct failed to initialize.");
        data_dispose(&data);
        return -1;
    }

    int calculation_variant = 0;
    switch (calculation_variant) {
        case 0:
        if (calculate_simple(data, result) < 0) {
            syslog(LOG_WARNING, "Compute Manager - Simple calculation failed.");
            data_dispose(&data);
            result_dispose(&result);
            return -1;
        }
        break;
        case 1:
        // if (calculate_linear(data, result) < 0) {
        //     syslog(LOG_WARNING, "Compute Manager - Simple calculation failed.");
        //     data_dispose(&data);
        //     result_dispose(&result);
        // }
        break;
    }

    int print_data = 1;
    if (print_data == 1) {
        syslog(LOG_INFO, "Data:\nIrradiance: %.2f\nCloudiness: %.2f\nTemperature: %.2f\nSpot-price: %.2f\nBattery charge: %.2f", data->irradiance, data->cloudiness, data->temperature, data->spot_price, data->battery_charge);
    }

    if (data_dispose(&data) < 0) {
        syslog(LOG_ERR, "Compute Manager - Data struct failed to dispose.");
        result_dispose(&result);
        return -1;
    }

    if (save_result(result) < 0) {
        syslog(LOG_WARNING, "Compute Manager - Failed to save result.");
        result_dispose(&result);
        return -1;
    }

    syslog(LOG_INFO, "Result:\nBuy electricity: %d\nUse solar: %d\nCharge battery: %d\nSell excess: %d", result->buy_electricity, result->use_solar, result->charge_battery, result->sell_excess);

    if (result_dispose(&result) < 0) {
        syslog(LOG_ERR, "Compute Manager - Result struct failed to dispose.");
        return -1;
    }

    return 0;
}

void cleanup(void) {
    syslog(LOG_INFO, "Compute Manager - Terminating.");
    closelog();
}