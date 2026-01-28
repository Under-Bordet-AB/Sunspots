#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

// #include "curl_client.h"
// #include "parsers.h"
// #include "database_manager.h"

int main(int argc, char* argv[]) {
    if (argc < 3) {
        fprintf(stderr, "Usage: %s <interval> <timeout>\n", argc > 0 ? argv[0] : "fetch_manager");
        return EXIT_FAILURE;
    }

    printf("Starting fetch manager.\n");

    // Parse arguments
    int interval = (int) atoi(argv[1]);
    int timeout = (int) atoi(argv[2]);

    // Test loop
    int iterations = 0;
    int max_iterations = 5;

    while (iterations < max_iterations) {
        printf("Running iteration %d/%d\n", iterations + 1, max_iterations);

        iterations++;

        if (iterations < max_iterations) {
            // sleep(interval);
            usleep(500000);
        }
    }

    return 0;
}

int fetch_from_smhi() {
    // curl from site
    // normalize with parser
    // save to database
    return 0;
}

int fetch_from_elpris() {
    // curl from site
    // normalize with parser
    // save to database
    return 0;
}