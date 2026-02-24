#define _POSIX_C_SOURCE 200809L // For VS Code to shut up about CLOCK_MONOTONIC
#include <time.h>
#include <stdio.h>
#include <errno.h>
#include <signal.h>
#include <unistd.h>
#include <stdlib.h>
#include <syslog.h>
#include <stdint.h>

#include "client_queue.h"
#include "http_constants.h"
#include "http_main.h"
#include "cJSON.h"

void cleanup(void);

#define ALLOW_STANDALONE_EXEC 1

int main() {
    atexit(cleanup);

    openlog("SUNSPOTS_HTTP_SERVER", LOG_PID, LOG_DAEMON);

    /* In any spawned module binary — replaces argv parsing */
    int    daemon_pid    = getppid();
    char  *sig_env       = getenv("SUNSPOTS_SIGNAL");
    if(sig_env == NULL) {
        printf("Missing environment variable: SUNSPOTS_SIGNAL\n");
        syslog(LOG_ERR, "<frontend/frontend_main.c> Missing environment variable: SUNSPOTS_SIGNAL");
        exit(EXIT_FAILURE);
    }
    int    sig_number    = atoi(sig_env);
    char  *config_blob   = getenv("SUNSPOTS_CONFIG");
    if(config_blob == NULL) {
        printf("Missing environment variable: SUNSPOTS_CONFIG\n");
        syslog(LOG_ERR, "<frontend/frontend_main.c> Missing environment variable: SUNSPOTS_CONFIG");
        exit(EXIT_FAILURE);
    }
    cJSON *cfg           = cJSON_Parse(config_blob);
    int    hb_interval   = cJSON_GetObjectItem(cfg, "heartbeat_interval")->valueint;

    if (kill(daemon_pid, sig_number) == -1) {
        printf("Could not signal the daemon PPID\n");
        syslog(LOG_ERR, "<frontend/frontend_main.c> PPID test signaling failed, is the argument correct?");
        exit(EXIT_FAILURE);
    }

    syslog(LOG_INFO, "<frontend/frontend_main.c> Initializing HTTP server...");
    http_server* srv = http_init();
    if(!srv)
        return 1;

    struct timespec sleep_duration = {
        .tv_sec = 0,
        .tv_nsec = 10 * 1000 * 1000  // 10 ms
    };

    struct timespec last, now;
    clock_gettime(CLOCK_MONOTONIC, &last);

    syslog(LOG_INFO, "<frontend/frontend_main.c> Frontend module is ready to go!");

    while(1)
    {
        clock_gettime(CLOCK_MONOTONIC, &now);
        int64_t elapsed_ns = (int64_t)(now.tv_sec - last.tv_sec) * 1000000000LL + (now.tv_nsec - last.tv_nsec);
        if(elapsed_ns >= hb_interval * 1000000000LL)
        {
            if (kill(daemon_pid, sig_number) == -1) {
                syslog(LOG_ERR, "<frontend/frontend_main.c> Could not signal daemon, panic!!!");
                exit(EXIT_FAILURE);
            }
            last = now;
        }

        int count = http_accept(srv);
        if(count == 0)
        {
            nanosleep(&sleep_duration, NULL);
        }
    }

    http_dispose(&srv);

    return 0;
}

void cleanup()
{
    closelog();
}