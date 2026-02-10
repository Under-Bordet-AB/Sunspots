#include "core/runtime_supervision.h"

#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#include "libs/json/cJSON.h"

time_t ss_current_aligned_slot_start(time_t now_utc, int interval_sec) {
    if (interval_sec <= 0 || now_utc < 0) {
        return -1;
    }
    time_t interval = (time_t)interval_sec;
    return now_utc - (now_utc % interval);
}

time_t ss_next_aligned_slot_start(time_t now_utc, int interval_sec) {
    time_t current = ss_current_aligned_slot_start(now_utc, interval_sec);
    if (current < 0) {
        return -1;
    }
    if (now_utc == current) {
        return current;
    }
    return current + (time_t)interval_sec;
}

int ss_compute_slot_window(time_t slot_start_utc, int slot_deadline_sec, int grace_sec, time_t* out_slot_deadline_utc,
                           time_t* out_supervisor_deadline_utc) {
    if (slot_start_utc < 0 || slot_deadline_sec < 0 || grace_sec < 0 || !out_slot_deadline_utc ||
        !out_supervisor_deadline_utc) {
        return -EINVAL;
    }

    *out_slot_deadline_utc = slot_start_utc + (time_t)slot_deadline_sec;
    *out_supervisor_deadline_utc = *out_slot_deadline_utc + (time_t)grace_sec;
    return 0;
}

int ss_retry_backoff_delay_sec(int attempt, int base_sec, int max_sec, int jitter_sec, unsigned int* io_seed) {
    if (attempt < 0 || base_sec <= 0 || max_sec <= 0 || jitter_sec < 0) {
        return -EINVAL;
    }

    int delay = base_sec;
    for (int i = 0; i < attempt; i++) {
        if (delay >= max_sec) {
            delay = max_sec;
            break;
        }
        if (delay > (INT_MAX / 2)) {
            delay = max_sec;
            break;
        }
        delay *= 2;
    }
    if (delay > max_sec) {
        delay = max_sec;
    }

    int jitter = 0;
    if (jitter_sec > 0) {
        unsigned int seed = io_seed ? *io_seed : (unsigned int)time(NULL);
        jitter = (int)(rand_r(&seed) % (unsigned int)(jitter_sec + 1));
        if (io_seed) {
            *io_seed = seed;
        }
    }
    return delay + jitter;
}

static int parse_i64_field(const cJSON* obj, const char* key, time_t* out) {
    if (!obj || !key || !out) {
        return -EINVAL;
    }
    const cJSON* item = cJSON_GetObjectItemCaseSensitive((cJSON*)obj, key);
    if (!cJSON_IsNumber(item)) {
        return -EINVAL;
    }
    double value = item->valuedouble;
    if (value < 0) {
        return -EINVAL;
    }
    *out = (time_t)value;
    return 0;
}

int ss_status_read_file(const char* path, ss_worker_status* out) {
    if (!path || !out) {
        return -EINVAL;
    }

    FILE* f = fopen(path, "rb");
    if (!f) {
        return -errno;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        (void)fclose(f);
        return -EIO;
    }
    long len = ftell(f);
    if (len < 0 || len > 1024 * 128 || fseek(f, 0, SEEK_SET) != 0) {
        (void)fclose(f);
        return -EIO;
    }

    char* buf = malloc((size_t)len + 1U);
    if (!buf) {
        (void)fclose(f);
        return -ENOMEM;
    }
    size_t n = fread(buf, 1, (size_t)len, f);
    if (n != (size_t)len) {
        free(buf);
        (void)fclose(f);
        return -EIO;
    }
    buf[n] = '\0';
    (void)fclose(f);

    cJSON* root = cJSON_Parse(buf);
    free(buf);
    if (!root) {
        return -EINVAL;
    }

    memset(out, 0, sizeof(*out));
    const cJSON* worker = cJSON_GetObjectItemCaseSensitive(root, "worker");
    const cJSON* ok = cJSON_GetObjectItemCaseSensitive(root, "ok");
    const cJSON* records = cJSON_GetObjectItemCaseSensitive(root, "records_written");
    const cJSON* error = cJSON_GetObjectItemCaseSensitive(root, "error");
    if (!cJSON_IsString(worker) || !worker->valuestring || !cJSON_IsBool(ok) || !cJSON_IsNumber(records) ||
        !cJSON_IsString(error) || !error->valuestring) {
        cJSON_Delete(root);
        return -EINVAL;
    }
    if (parse_i64_field(root, "slot_start_utc", &out->slot_start_utc) != 0 ||
        parse_i64_field(root, "slot_deadline_utc", &out->slot_deadline_utc) != 0 ||
        parse_i64_field(root, "completed_at_utc", &out->completed_at_utc) != 0) {
        cJSON_Delete(root);
        return -EINVAL;
    }

    (void)snprintf(out->worker, sizeof(out->worker), "%s", worker->valuestring);
    out->ok = cJSON_IsTrue(ok);
    out->records_written = records->valueint;
    (void)snprintf(out->error, sizeof(out->error), "%s", error->valuestring);
    cJSON_Delete(root);
    return 0;
}

int ss_status_write_atomic(const char* path, const ss_worker_status* status) {
    if (!path || !status) {
        return -EINVAL;
    }

    cJSON* root = cJSON_CreateObject();
    if (!root) {
        return -ENOMEM;
    }
    cJSON_AddStringToObject(root, "worker", status->worker);
    cJSON_AddNumberToObject(root, "slot_start_utc", (double)status->slot_start_utc);
    cJSON_AddNumberToObject(root, "slot_deadline_utc", (double)status->slot_deadline_utc);
    cJSON_AddNumberToObject(root, "completed_at_utc", (double)status->completed_at_utc);
    cJSON_AddBoolToObject(root, "ok", status->ok);
    cJSON_AddNumberToObject(root, "records_written", status->records_written);
    cJSON_AddStringToObject(root, "error", status->error);

    char* json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!json) {
        return -ENOMEM;
    }

    char tmp_path[512];
    (void)snprintf(tmp_path, sizeof(tmp_path), "%s.tmp.%ld", path, (long)getpid());
    FILE* f = fopen(tmp_path, "wb");
    if (!f) {
        cJSON_free(json);
        return -errno;
    }
    size_t len = strlen(json);
    size_t written = fwrite(json, 1, len, f);
    int close_rc = fclose(f);
    cJSON_free(json);
    if (written != len || close_rc != 0) {
        (void)unlink(tmp_path);
        return -EIO;
    }
    if (rename(tmp_path, path) != 0) {
        (void)unlink(tmp_path);
        return -errno;
    }
    return 0;
}

bool ss_status_is_slot_success(const ss_worker_status* status, const char* worker, time_t slot_start_utc) {
    if (!status || !worker || slot_start_utc < 0) {
        return false;
    }
    if (!status->ok) {
        return false;
    }
    if (strcmp(status->worker, worker) != 0) {
        return false;
    }
    return status->slot_start_utc == slot_start_utc;
}

ss_supervision_decision ss_supervision_evaluate(time_t now_utc, time_t supervisor_deadline_utc, bool slot_success) {
    if (slot_success) {
        return SS_SUPERVISION_HEALTHY;
    }
    if (now_utc > supervisor_deadline_utc) {
        return SS_SUPERVISION_RESTART;
    }
    return SS_SUPERVISION_WITHIN_GRACE;
}
