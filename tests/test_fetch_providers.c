#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "providers/openmeteo_provider.h"
#include "providers/smhi_provider.h"
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

static char* read_text(const char* path) {
    FILE* f = fopen(path, "rb");
    assert(f != NULL);
    assert(fseek(f, 0, SEEK_END) == 0);
    long sz = ftell(f);
    assert(sz >= 0);
    assert(fseek(f, 0, SEEK_SET) == 0);
    char* out = calloc((size_t) sz + 1, 1);
    assert(out != NULL);
    assert(fread(out, 1, (size_t) sz, f) == (size_t) sz);
    assert(fclose(f) == 0);
    return out;
}

void test_fetch_providers_emit_with_sdk(void) {
    must_mkdir("tests/tmp_fetchers");
    must_mkdir("tests/tmp_fetchers/.db");
    must_mkdir("tests/tmp_fetchers/endpoints");
    must_mkdir("tests/tmp_fetchers/logs");

    write_text("tests/tmp_fetchers/worker.json",
               "{"
               "\"version\":\"testv2\","
               "\"common\":{\"paths\":{\"db_file\":\"tests/tmp_fetchers/.db/database.jsonl\","
               "\"endpoints_dir\":\"tests/tmp_fetchers/endpoints\","
               "\"log_file\":\"tests/tmp_fetchers/logs/sunspots.jsonl\"},"
               "\"heartbeat\":{\"interval_ms\":1},\"schema\":{\"version\":1}},"
               "\"worker\":{\"name\":\"fetch_test\"}"
               "}");

    assert(setenv("SUNSPOTS_CONFIG_PATH", "tests/tmp_fetchers/worker.json", 1) == 0);
    assert(setenv("SUNSPOTS_CONFIG_VERSION", "testv2", 1) == 0);

    char* argv[] = {"fetch_test", "1", NULL};
    sssdk_runtime rt;
    assert(sssdk_bootstrap(&rt, 2, argv) == 0);

    char* om = read_text("tests/fixtures/openmeteo_sample.json");
    char* smhi = read_text("tests/fixtures/smhi_sample.json");

    int om_count = openmeteo_emit_from_json(&rt, om, 1111);
    int smhi_count = smhi_emit_from_json(&rt, smhi, 2222);
    free(om);
    free(smhi);
    assert(om_count == 3);
    assert(smhi_count == 9);

    assert(sssdk_shutdown(&rt) == 0);

    char* db = read_text("tests/tmp_fetchers/.db/database.jsonl");
    assert(strstr(db, "\"source\":\"openmeteo\"") != NULL);
    assert(strstr(db, "\"source\":\"smhi\"") != NULL);
    assert(strstr(db, "\"type\":\"temperature_c\"") != NULL);
    assert(strstr(db, "\"type\":\"solar_irradiance_wm2\"") != NULL);
    assert(strstr(db, "\"type\":\"wind_speed_ms\"") != NULL);
    assert(strstr(db, "\"valid_time\":\"2026-02-05T12:00:00Z\"") != NULL);
    free(db);

    printf("[PASS] Fetch Providers SDK Emit\n");
}

int run_fetch_provider_tests(void) {
    test_fetch_providers_emit_with_sdk();
    return 1;
}
