/*
 * Simple multithreaded TCP server (thread pool)
 * - Main thread accepts connections
 * - Worker threads handle clients
 */

#include <stdio.h>
#include <unistd.h>

#include "frontend/client_queue.h"
#include "frontend/http_constants.h"
#include "frontend/http_main.h"
#include "libs/jj_log/jj_log.h"

int main() {
    http_server* srv = http_init();

    int global_count = 0;
    while(1)
    {
        int count = http_accept(srv);
        if(count == 0)
        {
            usleep(10000);
        } else {
            global_count += count;
            if(global_count >= 1000)
                break;
        }
    }

    http_dispose(&srv);

    return 0;
}