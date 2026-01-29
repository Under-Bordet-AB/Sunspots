#include <stdio.h>
#include <unistd.h>

#include "frontend/client_queue.h"
#include "frontend/http_constants.h"
#include "frontend/http_main.h"
#include "libs/jj_log/jj_log.h"

int main() {
    http_server* srv = http_init();
    if(!srv)
        return 1;

    while(1)
    {
        int count = http_accept(srv);
        if(count == 0)
        {
            usleep(10000);
        }
    }

    http_dispose(&srv);

    return 0;
}