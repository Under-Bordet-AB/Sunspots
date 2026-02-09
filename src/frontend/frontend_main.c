#include <stdio.h>
#include <unistd.h>

#include "client_queue.h"
#include "http_constants.h"
#include "http_main.h"

int main() {
    http_server* srv = http_init();
    if(!srv)
        return 1;

    struct timespec ts = {
        .tv_sec = 0,
        .tv_nsec = 10 * 1000 * 1000  // 10 ms
    };

    while(1)
    {
        int count = http_accept(srv);
        if(count == 0)
        {
            nanosleep(&ts, NULL);
        }
    }

    http_dispose(&srv);

    return 0;
}