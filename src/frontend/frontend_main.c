#define _POSIX_C_SOURCE 200809L // For VS Code to shut up about CLOCK_MONOTONIC
#include <time.h>
#include <stdio.h>
#include <errno.h>
#include <signal.h>
#include <unistd.h>
#include <stdlib.h>
#include <syslog.h>
#include <stdint.h>
#include <string.h>

#include "client_queue.h"
#include "http_constants.h"
#include "http_parser.h"
#include "http_main.h"
#include "cJSON.h"

void cleanup(void);

#define ALLOW_STANDALONE_EXEC 1

int main() {
    atexit(cleanup);

    openlog("SUNSPOTS_HTTP_SERVER", LOG_PID, LOG_DAEMON);

    int skipped = 1;

    /* In any spawned module binary — replaces argv parsing */
    int    daemon_pid    = getppid();
    char  *sig_env       = getenv("SUNSPOTS_SIGNAL");
    if(sig_env == NULL) {
        if(ALLOW_STANDALONE_EXEC)
            goto skip;
        printf("Missing environment variable: SUNSPOTS_SIGNAL\n");
        syslog(LOG_ERR, "<frontend/frontend_main.c> Missing environment variable: SUNSPOTS_SIGNAL");
        exit(EXIT_FAILURE);
    }
    int    sig_number    = (int)strtoul(sig_env, NULL, 10);
    char  *config_blob   = getenv("SUNSPOTS_CONFIG");
    if(config_blob == NULL) {
        printf("Missing environment variable: SUNSPOTS_CONFIG\n");
        syslog(LOG_ERR, "<frontend/frontend_main.c> Missing environment variable: SUNSPOTS_CONFIG");
        exit(EXIT_FAILURE);
    }
    cJSON *cfg           = cJSON_Parse(config_blob);

    int hb_interval = 5;


    // Load configs safely

    cJSON *js_temp = cJSON_GetObjectItem(cfg, "heartbeat_interval");
    if(js_temp != NULL) { hb_interval = js_temp->valueint; } else { syslog(LOG_WARNING, "<frontend/frontend_main.c> SUNSPOTS_CONFIG does not contain heartbeat_interval, using default value."); }

    js_temp = cJSON_GetObjectItem(cfg, "server_port_http");
    if(js_temp != NULL) { HTTP_PORT = js_temp->valueint; } else { syslog(LOG_WARNING, "<frontend/frontend_main.c> SUNSPOTS_CONFIG does not contain server_port_http, using default value."); }

    js_temp = cJSON_GetObjectItem(cfg, "server_threads");
    if(js_temp != NULL) { LISTENER_COUNT = js_temp->valueint; } else { syslog(LOG_WARNING, "<frontend/frontend_main.c> SUNSPOTS_CONFIG does not contain server_threads, using default value."); }

    js_temp = cJSON_GetObjectItem(cfg, "server_listen_queue");
    if(js_temp != NULL) { LISTEN_QUEUE = js_temp->valueint; } else { syslog(LOG_WARNING, "<frontend/frontend_main.c> SUNSPOTS_CONFIG does not contain server_listen_queue, using default value."); }

    js_temp = cJSON_GetObjectItem(cfg, "client_queue_size");
    if(js_temp != NULL) { QUEUE_SIZE = js_temp->valueint; } else { syslog(LOG_WARNING, "<frontend/frontend_main.c> SUNSPOTS_CONFIG does not contain client_queue_size, using default value."); }

    js_temp = cJSON_GetObjectItem(cfg, "allow_file_search");
    if(js_temp != NULL) { ALLOW_SEARCH = js_temp->valueint; } else { syslog(LOG_WARNING, "<frontend/frontend_main.c> SUNSPOTS_CONFIG does not contain allow_file_search, using default value."); }

    js_temp = cJSON_GetObjectItem(cfg, "file_search_dir");
    if(js_temp != NULL) {
        int pathlen = strlen(js_temp->valuestring);
        FILE_SEARCH_DIR = malloc(pathlen + 1);
        if(FILE_SEARCH_DIR == NULL)
        {
            syslog(LOG_ERR, "<frontend/frontend_main.c> Memory allocation error for FILE_SEARCH_DIR, exiting.");
            exit(EXIT_FAILURE);
        }
        strncpy(FILE_SEARCH_DIR, js_temp->valuestring, pathlen + 1);
    } else {
        syslog(LOG_WARNING, "<frontend/frontend_main.c> SUNSPOTS_CONFIG does not contain file_search_dir, using default value.");
        FILE_SEARCH_DIR = "./htdocs";
    }

    cJSON *aliases_json = cJSON_GetObjectItem(cfg, "aliases");
    if (aliases_json && cJSON_IsArray(aliases_json)) {

        URL_ALIASES = LinkedList_create();

        cJSON *alias_json;
        cJSON_ArrayForEach(alias_json, aliases_json) {

            cJSON *url = cJSON_GetObjectItem(alias_json, "target_url");
            cJSON *file = cJSON_GetObjectItem(alias_json, "target_file");

            if (!cJSON_IsString(url) || !cJSON_IsString(file))
                continue;

            url_alias *alias = malloc(sizeof(url_alias));
            alias->target_url = strdup(url->valuestring);
            alias->target_file = strdup(file->valuestring);

            str_to_lower(alias->target_url);

            LinkedList_append(URL_ALIASES, alias);
        }
    }

    if (kill(daemon_pid, sig_number) == -1) {
        printf("Could not signal the daemon PPID\n");
        syslog(LOG_ERR, "<frontend/frontend_main.c> PPID test signaling failed, is the argument correct?");
        exit(EXIT_FAILURE);
    }

    skipped = 0;

skip:

    syslog(LOG_INFO, "<frontend/frontend_main.c> Initializing HTTP server...");
    http_server* srv = http_init();
    if(!srv)
        return 1;

    struct timespec sleep_duration = {
        .tv_sec = 0,
        .tv_nsec = 10L * 1000L * 1000L  // 10 ms
    };

    struct timespec last, now;
    clock_gettime(CLOCK_MONOTONIC, &last);

    syslog(LOG_INFO, "<frontend/frontend_main.c> Frontend module is ready to go!");

    while(1)
    {
        if(!skipped)
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