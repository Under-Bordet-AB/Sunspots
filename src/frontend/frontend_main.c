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

void cleanup(void);

#define ALLOW_STANDALONE_EXEC 1

int main(int argc, char* argv[]) {
    atexit(cleanup);

    openlog("SUNSPOTS_HTTP_SERVER", LOG_PID, LOG_DAEMON);

    int received = 0;
    if(argc < 3)
    {
        if(!ALLOW_STANDALONE_EXEC)
        {
            printf("Usage: /path/to/frontend <PPID> <Heartbeats/sec>\n");
            syslog(LOG_ERR, "<frontend/frontend_main.c> Missing command line arguments: /path/to/frontend <PPID> <Heartbeats/sec>");
            exit(EXIT_FAILURE);
        }
    } else {
        received = 1;
    }

    long parent_pid, beat_freq = 0;

    if(received)
    {
        char* endptr;
        parent_pid = strtol(argv[1], &endptr, 10);
        if(errno == ERANGE)
        {
            printf("Invalid PPID argument\n");
            syslog(LOG_ERR, "<frontend/frontend_main.c> Could not parse PPID argument");
            exit(EXIT_FAILURE);
        }
        beat_freq = strtol(argv[2], &endptr, 10);
        if(errno == ERANGE)
        {
            printf("Invalid heartbeat frequency argument\n");
            syslog(LOG_ERR, "<frontend/frontend_main.c> Could not parse heartbeat frequency argument");
            exit(EXIT_FAILURE);
        }

        if (kill(parent_pid, SIGRTMIN) == -1) {
            printf("Could not signal the provided PPID\n");
            syslog(LOG_ERR, "<frontend/frontend_main.c> PPID test signaling failed, is the argument correct?");
            exit(EXIT_FAILURE);
        }
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
        if(received)
        {
            int64_t elapsed_ns = (int64_t)(now.tv_sec - last.tv_sec) * 1000000000LL + (now.tv_nsec - last.tv_nsec);
            if(elapsed_ns >= beat_freq * 1000000000LL)
            {
                if (kill(parent_pid, SIGRTMIN) == -1) {
                    syslog(LOG_ERR, "<frontend/frontend_main.c> Could not signal daemon, panic!!!");
                    exit(EXIT_FAILURE);
                }
                last = now;
            }
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