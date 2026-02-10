#ifndef RUNTIME_SUPERVISION_H
#define RUNTIME_SUPERVISION_H

#include <stdbool.h>
#include <time.h>

typedef struct ss_worker_status {
    char worker[64];
    time_t slot_start_utc;
    time_t slot_deadline_utc;
    time_t completed_at_utc;
    bool ok;
    int records_written;
    char error[128];
} ss_worker_status;

typedef enum ss_supervision_decision {
    SS_SUPERVISION_HEALTHY = 0,
    SS_SUPERVISION_WITHIN_GRACE = 1,
    SS_SUPERVISION_RESTART = 2
} ss_supervision_decision;

time_t ss_current_aligned_slot_start(time_t now_utc, int interval_sec);
time_t ss_next_aligned_slot_start(time_t now_utc, int interval_sec);
int ss_compute_slot_window(time_t slot_start_utc, int slot_deadline_sec, int grace_sec, time_t* out_slot_deadline_utc,
                           time_t* out_supervisor_deadline_utc);
int ss_retry_backoff_delay_sec(int attempt, int base_sec, int max_sec, int jitter_sec, unsigned int* io_seed);

int ss_status_read_file(const char* path, ss_worker_status* out);
int ss_status_write_atomic(const char* path, const ss_worker_status* status);
bool ss_status_is_slot_success(const ss_worker_status* status, const char* worker, time_t slot_start_utc);
ss_supervision_decision ss_supervision_evaluate(time_t now_utc, time_t supervisor_deadline_utc, bool slot_success);

#endif
