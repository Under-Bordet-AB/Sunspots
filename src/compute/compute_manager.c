#include <stdio.h>
#include <stdlib.h>
#include <syslog.h>
#include <unistd.h>

#include "algorithms/compute_lp.h"

#define DB_PATH "db_path" // Not implemented yet

typedef struct compute_data_t {
    double irradiance[96];
    double cloudiness[96];
    double temperature[96];

    double elpris[96];
} compute_data_t;

typedef struct result_t {
    double x;
    double y;
    double z;
    double v;
} result_t;

void cleanup(void);

int main() {
    atexit(cleanup);

    openlog("SUNSPOTS_COMPUTE_MANAGER", LOG_PID, LOG_DAEMON);

    syslog(LOG_INFO, "Compute Manager - Starting...");

    syslog(LOG_INFO, "Compute Manager - I think I want to compute something today...");

    // Load data
    // Compute
    // Save result

    exit(EXIT_SUCCESS);
}

void cleanup(void) {
    syslog(LOG_INFO, "Compute Manager - Terminating.");
    closelog();
}