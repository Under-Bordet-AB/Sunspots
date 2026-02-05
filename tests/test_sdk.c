#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "libs/json/cJSON.h"
#include "sdk/sunspots_sdk.h"

static void must_mkdir(const char* path) {
    int rc = mkdir(path, 0755);
    if (rc != 0 && access(path, F_OK) != 0) {
        perror("mkdir");
        exit(1);
    }
}

static void write_text(const char* path, const char* txt) {
    FILE* f = fopen(path, "wb");
    assert(f != NULL);
    size_t n = fwrite(txt, 1, strlen(txt), f);
    assert(n == strlen(txt));
    assert(fclose(f) == 0);
}

void test_sdk_emit_read_publish(void) {
    sssdk_type_id parsed = SSSDK_TYPE_INVALID;
    assert(sssdk_type_parse("temperature_c", &parsed) == 0);
    assert(parsed == SSSDK_TYPE_TEMPERATURE_C);
    assert(sssdk_type_name(SSSDK_TYPE_SOLAR_IRRADIANCE_WM2) != NULL);

    must_mkdir("tests/tmp_sdk");
    must_mkdir("tests/tmp_sdk/.db");
    must_mkdir("tests/tmp_sdk/endpoints");
    must_mkdir("tests/tmp_sdk/logs");
    (void) remove("tests/tmp_sdk/.db/database.jsonl");
    (void) remove("tests/tmp_sdk/endpoints/energy_plan.json");

    const char* slice_path = "tests/tmp_sdk/worker.json";
    write_text(slice_path,
               "{"
               "\"version\":\"testv1\","
               "\"common\":{\"paths\":{\"db_file\":\"tests/tmp_sdk/.db/database.jsonl\","
               "\"endpoints_dir\":\"tests/tmp_sdk/endpoints\","
               "\"log_file\":\"tests/tmp_sdk/logs/sunspots.jsonl\"},"
               "\"heartbeat\":{\"interval_ms\":1},\"schema\":{\"version\":1}},"
               "\"worker\":{\"name\":\"test_worker\",\"poll_interval_minutes\":2}"
               "}");

    assert(setenv("SUNSPOTS_CONFIG_PATH", slice_path, 1) == 0);
    assert(setenv("SUNSPOTS_CONFIG_VERSION", "testv1", 1) == 0);

    char* argv[] = {"test_worker", "1", NULL};
    sssdk_runtime rt;
    assert(sssdk_bootstrap(&rt, 2, argv) == 0);
    assert(sssdk_get_poll_interval_ms(&rt, 500) == 120000);

    sssdk_record rec1 = {.source = "src1", .type = "solar_irradiance_wm2", .timestamp = 100, .payload_json = "{\"value\":100}"};
    sssdk_record rec2 = {.source = "src2",
                         .type = NULL,
                         .type_id = SSSDK_TYPE_SOLAR_IRRADIANCE_WM2,
                         .timestamp = 200,
                         .payload_json = "{\"value\":300}"};
    assert(sssdk_emit_record(&rt, &rec1) == 0);
    assert(sssdk_emit_record(&rt, &rec2) == 0);
    sssdk_record rec_bad = {.source = "src3", .type = "nonexistent_type", .timestamp = 300, .payload_json = "{\"value\":1}"};
    assert(sssdk_emit_record(&rt, &rec_bad) != 0);

    sssdk_reader rd;
    assert(sssdk_reader_open(&rt, &rd, 0) == 0);
    int seen = 0;
    for (;;) {
        sssdk_record out;
        int rc = sssdk_reader_next(&rd, &out);
        if (rc == 0) {
            break;
        }
        assert(rc == 1);
        assert(out.type_id == SSSDK_TYPE_SOLAR_IRRADIANCE_WM2);
        cJSON* payload = cJSON_Parse(out.payload_json);
        assert(payload != NULL);
        cJSON_Delete(payload);
        seen++;
    }
    assert(sssdk_reader_close(&rd) == 0);
    assert(seen == 2);

    assert(sssdk_publish_endpoint(&rt, "energy_plan", "{\"ok\":true}") == 0);
    FILE* ep = fopen("tests/tmp_sdk/endpoints/energy_plan.json", "rb");
    assert(ep != NULL);
    assert(fclose(ep) == 0);

    assert(sssdk_shutdown(&rt) == 0);
    printf("[PASS] SDK Emit/Read/Publish\n");
}

int run_sdk_tests(void) {
    test_sdk_emit_read_publish();
    return 1;
}
