#ifndef SUNSPOTS_SDK_H
#define SUNSPOTS_SDK_H

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <sys/types.h>

#include "config/config.h"
#include "log/sunspots_log.h"
#include "sdk/canonical_types.h"

typedef struct sssdk_runtime {
    config* cfg;
    const config* common;
    const config* worker;
    char version[64];
    char db_file[512];
    char endpoints_dir[512];
    char log_file[512];
    int heartbeat_ms;
    pid_t parent_pid;
    bool running;
    sunspots_log* logger;
} sssdk_runtime;

typedef struct sssdk_record {
    const char* source;
    const char* type;
    sssdk_type_id type_id;
    long timestamp;
    const char* payload_json;
} sssdk_record;

typedef struct sssdk_reader {
    FILE* fp;
    char line_buf[4096];
    char source_buf[128];
    char type_buf[128];
    char payload_buf[2048];
    long timestamp;
} sssdk_reader;

int sssdk_bootstrap(sssdk_runtime* rt, int argc, char** argv);
int sssdk_should_run(sssdk_runtime* rt);
int sssdk_heartbeat(sssdk_runtime* rt);
int sssdk_sleep_interval(sssdk_runtime* rt);
int sssdk_shutdown(sssdk_runtime* rt);
int sssdk_get_poll_interval_ms(const sssdk_runtime* rt, int default_ms);

int sssdk_emit_record(sssdk_runtime* rt, const sssdk_record* rec);
int sssdk_reader_open(sssdk_runtime* rt, sssdk_reader* rd, int mode);
int sssdk_reader_next(sssdk_reader* rd, sssdk_record* out);
int sssdk_reader_close(sssdk_reader* rd);
int sssdk_publish_endpoint(sssdk_runtime* rt, const char* endpoint_name, const char* json_body);

int sssdk_log_info(sssdk_runtime* rt, const char* msg);
int sssdk_log_warn(sssdk_runtime* rt, const char* msg);
int sssdk_log_error(sssdk_runtime* rt, const char* msg);

#endif
