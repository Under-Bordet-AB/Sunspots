#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "libs/json/cJSON.h"
#include "sdk/sunspots_sdk.h"

static volatile sig_atomic_t g_running = 1;

static void on_term(int sig) {
    (void) sig;
    g_running = 0;
}

int main(int argc, char** argv) {
    struct sigaction sa;
    sa.sa_handler = on_term;
    sa.sa_flags = 0;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGINT, &sa, NULL);

    sssdk_runtime rt;
    if (sssdk_bootstrap(&rt, argc, argv) != 0) {
        return EXIT_FAILURE;
    }

    int poll_ms = sssdk_get_poll_interval_ms(&rt, rt.heartbeat_ms);
    int run_once = config_get_bool_or(rt.worker, "run_once", false) ? 1 : 0;
    if (poll_ms > 0) {
        rt.heartbeat_ms = poll_ms;
    }

    while (g_running && sssdk_should_run(&rt)) {
        sssdk_reader rd;
        int rc = sssdk_reader_open(&rt, &rd, 0);
        double sum = 0.0;
        int count = 0;
        long batch_ts = -1;
        if (rc == 0) {
            sssdk_record rec;
            while ((rc = sssdk_reader_next(&rd, &rec)) == 1) {
                if (rec.type_id != SSSDK_TYPE_TEMPERATURE_C) {
                    continue;
                }
                if (!rec.source || rec.source[0] == '\0' || strcmp(rec.source, "smhi") != 0) {
                    continue;
                }
                cJSON* payload = cJSON_Parse(rec.payload_json);
                if (!payload) {
                    continue;
                }
                cJSON* value = cJSON_GetObjectItemCaseSensitive(payload, "value");
                if (!cJSON_IsNumber(value)) {
                    cJSON_Delete(payload);
                    continue;
                }

                if (rec.timestamp > batch_ts) {
                    batch_ts = rec.timestamp;
                    sum = value->valuedouble;
                    count = 1;
                } else if (rec.timestamp == batch_ts) {
                    sum += value->valuedouble;
                    count++;
                }

                cJSON_Delete(payload);
            }
            (void) sssdk_reader_close(&rd);
        }

        double avg = (count > 0) ? (sum / count) : 0.0;
        char endpoint[320];
        int n = snprintf(endpoint, sizeof(endpoint),
                         "{\"source\":\"smhi\",\"metric\":\"temperature_c\",\"batch_timestamp\":%ld,"
                         "\"samples\":%d,\"average_temperature_c\":%.4f}",
                         batch_ts, count, avg);
        if (n > 0 && (size_t) n < sizeof(endpoint)) {
            if (sssdk_publish_endpoint(&rt, "smhi_avg_temperature", endpoint) != 0) {
                (void) sssdk_log_error(&rt, "failed to publish smhi_avg_temperature endpoint");
            }
        }

        (void) sssdk_heartbeat(&rt);
        if (run_once) {
            break;
        }
        (void) sssdk_sleep_interval(&rt);
    }

    (void) sssdk_shutdown(&rt);
    return EXIT_SUCCESS;
}
