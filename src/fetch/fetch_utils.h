#include <time.h>
#include "../libs/json/cJSON.h"
#include "../libs/curly.h"

int fetch_from_url(char* url, char** buffer, int timeout) {
    curly_t* curly = NULL;
    if (curly_init(&curly) < 0) {
        printf("Curly failed to initiate\n");
        return -1;
    }

    if (curly_make_request(&curly, url) < 0) {
        printf("Request failed\n");
        curly_cleanup(&curly);
        return -1;
    }

    struct timespec start_ts;
    int iterations = 0;
    int max_iterations = (timeout > 0) ? (timeout * 100) : 3000;
    long timeout_ms = (timeout > 0) ? ((long)timeout * 1000L) : 30000L;
    if (clock_gettime(CLOCK_MONOTONIC, &start_ts) != 0) {
        start_ts.tv_sec = 0;
        start_ts.tv_nsec = 0;
    }

    while (curly_poll(&curly) == 0) {
        if (curly_is_running(&curly) == 0) {
            break;
        }

        if (start_ts.tv_sec != 0 || start_ts.tv_nsec != 0) {
            struct timespec now_ts;
            if (clock_gettime(CLOCK_MONOTONIC, &now_ts) == 0) {
                long elapsed_ms = (long)((now_ts.tv_sec - start_ts.tv_sec) * 1000L) + (long)((now_ts.tv_nsec - start_ts.tv_nsec) / 1000000L);
                if (elapsed_ms >= timeout_ms) {
                    printf("Fetching from URL timed out\n");
                    curly_cleanup(&curly);
                    return -1;
                }
            }
        } else {
            iterations++;
            if (iterations >= max_iterations) {
                printf("Fetching from URL timed out\n");
                curly_cleanup(&curly);
                return -1;
            }
        }

        struct timespec ts;
        ts.tv_sec = 0;
        ts.tv_nsec = 10 * 1000 * 1000;
        nanosleep(&ts, NULL);
    }

    char* response = NULL;
    if (curly_read_response(&curly, &response) < 0) {
        printf("Reading response failed\n");
        curly_cleanup(&curly);
        return -1;
    }
    
    if (response) {
        *buffer = response;
    }

    if (curly_cleanup(&curly) < 0) {
        printf("Failed to cleanup curly\n");
        return -1;
    }

    return 0;
}
