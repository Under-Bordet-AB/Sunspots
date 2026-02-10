#include <errno.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "config/config.h"
#include "core/worker_bootstrap.h"
#include "libs/json/cJSON.h"
#include "sdk/ss_sdk.h"

static volatile sig_atomic_t g_running = 1;

static void on_term(int sig) {
    (void)sig;
    g_running = 0;
}

static int unit_multiplier_ms(const char *unit) {
    if (!unit || unit[0] == '\0') {
        return 1000;
    }
    if (strcmp(unit, "ms") == 0 || strcmp(unit, "millisecond") == 0 || strcmp(unit, "milliseconds") == 0) {
        return 1;
    }
    if (strcmp(unit, "s") == 0 || strcmp(unit, "sec") == 0 || strcmp(unit, "second") == 0 ||
        strcmp(unit, "seconds") == 0) {
        return 1000;
    }
    if (strcmp(unit, "m") == 0 || strcmp(unit, "min") == 0 || strcmp(unit, "minute") == 0 ||
        strcmp(unit, "minutes") == 0) {
        return 60 * 1000;
    }
    if (strcmp(unit, "h") == 0 || strcmp(unit, "hr") == 0 || strcmp(unit, "hour") == 0 || strcmp(unit, "hours") == 0) {
        return 60 * 60 * 1000;
    }
    return -1;
}

static int get_poll_interval_ms(const config *worker, int default_ms) {
    int value = config_get_int_or(worker, "poll_interval_hours", -1);
    if (value > 0) {
        return value * 60 * 60 * 1000;
    }

    value = config_get_int_or(worker, "poll_interval_minutes", -1);
    if (value > 0) {
        return value * 60 * 1000;
    }

    value = config_get_int_or(worker, "poll_interval_seconds", -1);
    if (value > 0) {
        return value * 1000;
    }

    value = config_get_int_or(worker, "poll_interval.value", -1);
    if (value > 0) {
        const char *unit = config_get_string_or(worker, "poll_interval.unit", "seconds");
        int multiplier = unit_multiplier_ms(unit);
        if (multiplier > 0) {
            return value * multiplier;
        }
    }

    value = config_get_int_or(worker, "poll_interval_ms", -1);
    if (value > 0) {
        return value;
    }

    return default_ms;
}

static int ensure_parent_dirs(const char *path) {
    char *tmp = strdup(path);
    char *p;

    if (tmp == NULL) {
        return -ENOMEM;
    }

    p = tmp;
    while (*p != '\0') {
        if (*p == '/') {
            *p = '\0';
            if (tmp[0] != '\0' && mkdir(tmp, 0775) != 0 && errno != EEXIST) {
                free(tmp);
                return -errno;
            }
            *p = '/';
        }
        ++p;
    }

    free(tmp);
    return 0;
}

static int publish_endpoint(const char *endpoints_dir, const char *name, const char *json_body) {
    char tmp_path[1024];
    char out_path[1024];
    FILE *f;

    if (!endpoints_dir || !name || !json_body) {
        return -EINVAL;
    }

    if (ensure_parent_dirs(endpoints_dir) != 0) {
        return -EIO;
    }

    int n1 = snprintf(tmp_path, sizeof(tmp_path), "%s/.%s.tmp", endpoints_dir, name);
    int n2 = snprintf(out_path, sizeof(out_path), "%s/%s.json", endpoints_dir, name);
    if (n1 <= 0 || n2 <= 0 || (size_t)n1 >= sizeof(tmp_path) || (size_t)n2 >= sizeof(out_path)) {
        return -ENAMETOOLONG;
    }

    f = fopen(tmp_path, "wb");
    if (f == NULL) {
        return -errno;
    }

    size_t len = strlen(json_body);
    if (fwrite(json_body, 1, len, f) != len || fclose(f) != 0) {
        return -EIO;
    }

    if (rename(tmp_path, out_path) != 0) {
        (void)unlink(tmp_path);
        return -errno;
    }

    return 0;
}

int main(int argc, char **argv) {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = on_term;
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGINT, &sa, NULL);

    config *cfg = NULL;
    char version[64];
    if (worker_cfg_load_from_env(&cfg, version, sizeof(version)) != 0) {
        return EXIT_FAILURE;
    }

    const config *common = NULL;
    const config *worker = NULL;
    if (worker_cfg_get_common(cfg, &common) != 0 || worker_cfg_get_section(cfg, &worker) != 0) {
        config_destroy(&cfg);
        return EXIT_FAILURE;
    }

    char db_file[512];
    char endpoints_dir[512];
    if (config_get_string(common, "paths.db_file", db_file, sizeof(db_file)) != 0 ||
        config_get_string(common, "paths.endpoints_dir", endpoints_dir, sizeof(endpoints_dir)) != 0) {
        config_destroy(&cfg);
        return EXIT_FAILURE;
    }

    if (setenv("SS_SDK_DB_PATH", db_file, 1) != 0) {
        config_destroy(&cfg);
        return EXIT_FAILURE;
    }

    char *cfg_json = NULL;
    if (config_export_subtree_json(cfg, "", &cfg_json) == 0 && cfg_json != NULL) {
        (void)setenv("SUNSPOTS_CONFIG", cfg_json, 1);
        cJSON_free(cfg_json);
    }

    int heartbeat_ms = config_get_int_or(common, "heartbeat.interval_ms", 1000);
    if (heartbeat_ms <= 0) {
        heartbeat_ms = 1000;
    }
    int poll_ms = get_poll_interval_ms(worker, heartbeat_ms);
    bool run_once = config_get_bool_or(worker, "run_once", false);

    if (argc < 2) {
        config_destroy(&cfg);
        return EXIT_FAILURE;
    }
    errno = 0;
    char *endptr = NULL;
    long parent_pid = strtol(argv[1], &endptr, 10);
    if (errno != 0 || endptr == argv[1] || *endptr != '\0' || parent_pid <= 1) {
        config_destroy(&cfg);
        return EXIT_FAILURE;
    }

    while (g_running) {
        ss_sdk_record *rows = NULL;
        size_t count = 0;
        double sum = 0.0;
        size_t samples = 0;
        int64_t newest_ts = -1;

        if (ss_sdk_db_get_last_weeks(8, &rows, &count) == SS_SDK_OK) {
            for (size_t i = 0; i < count; i++) {
                const ss_sdk_record *rec = &rows[i];
                if (rec->metric != SS_METRIC_WEATHER_TEMPERATURE_AIR_2M_C) {
                    continue;
                }
                if (rec->data_kind != SS_SDK_DATA_OBSERVATION) {
                    continue;
                }
                if (!rec->source_api || strcmp(rec->source_api, "smhi") != 0) {
                    continue;
                }
                if (rec->value_type != SS_SDK_VALUE_F64) {
                    continue;
                }

                if (rec->ts_start_utc > newest_ts) {
                    newest_ts = rec->ts_start_utc;
                    sum = rec->value.f64;
                    samples = 1;
                } else if (rec->ts_start_utc == newest_ts) {
                    sum += rec->value.f64;
                    samples++;
                }
            }
            ss_sdk_db_free_records(rows);
        }

        double avg = (samples > 0) ? (sum / (double)samples) : 0.0;
        char endpoint[320];
        int n = snprintf(endpoint, sizeof(endpoint),
                         "{\"source\":\"smhi\",\"metric\":\"temperature_c\",\"batch_timestamp\":%lld,"
                         "\"samples\":%zu,\"average_temperature_c\":%.4f}",
                         (long long)newest_ts, samples, avg);
        if (n > 0 && (size_t)n < sizeof(endpoint)) {
            if (publish_endpoint(endpoints_dir, "smhi_avg_temperature", endpoint) != 0) {
                SS_LOG_ERROR("calc.smhi_avg_temp.publish_failed", "failed to publish smhi_avg_temperature endpoint");
            }
        }

        (void)kill((pid_t)parent_pid, SIGRTMIN);
        if (run_once) {
            break;
        }

        struct timespec ts;
        ts.tv_sec = poll_ms / 1000;
        ts.tv_nsec = (long)(poll_ms % 1000) * 1000000L;
        (void)nanosleep(&ts, NULL);
    }

    ss_sdk_shutdown();
    config_destroy(&cfg);
    return EXIT_SUCCESS;
}
