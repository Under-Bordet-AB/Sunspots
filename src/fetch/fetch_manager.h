#ifndef FETCH_MANAGER_H
#define FETCH_MANAGER_H

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

// #include "curl_client.h"
// #include "parser_smhi.h"
// #include "parser_elpris.h"
// #include "database_manager.h"

typedef struct fetch_manager_t {
    int interval;
    int timeout;
} fetch_manager_t;

void fetch_dispose();

void fetch_work(int interval, int timeout);

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