#ifndef FETCH_MANAGER_H
#define FETCH_MANAGER_H

#include <stdio.h>

// #include "curl_client.h"
// #include "parser_smhi.h"
// #include "parser_elpris.h"
// #include "database_manager.h"

typedef struct fetch_manager {
} fetch_manager;

void fetch_start() {
    printf("Starting fetch manager.\n");

    while (1) {
        // once every 15 minutes
        // start fetch threads from the functions below, poll them, save to database when done
        // signal to parent that the fetching is done
    }
}

void fetch_init() {
    printf("Initializing fetch manager.\n");

    fetch_start();
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

#endif