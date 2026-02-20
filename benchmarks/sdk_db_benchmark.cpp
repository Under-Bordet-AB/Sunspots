#include <benchmark/benchmark.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>

#include <unistd.h>

extern "C" {
#include "sdk/ss_sdk.h"
}

namespace {

std::string g_db_dir;
std::string g_db_path;

int64_t align_slot(int64_t ts_utc)
{
    if (ts_utc < 0) {
        return 0;
    }
    return ts_utc - (ts_utc % 900);
}

bool ensure_bench_db_path()
{
    if (!g_db_path.empty()) {
        return true;
    }

    char tpl[] = "/tmp/sunspots_sdk_bench_XXXXXX";
    char *dir = mkdtemp(tpl);
    if (dir == NULL) {
        return false;
    }

    g_db_dir = dir;
    g_db_path = g_db_dir + "/sdk_bench.db";
    return setenv("SS_SDK_DB_PATH", g_db_path.c_str(), 1) == 0;
}

bool reset_db_file()
{
    if (!ensure_bench_db_path()) {
        return false;
    }

    ss_sdk_shutdown();
    (void)unlink(g_db_path.c_str());
    return true;
}

ss_sdk_record make_record(int64_t ts_start, double value)
{
    ss_sdk_record rec;
    std::memset(&rec, 0, sizeof(rec));
    rec.metric = SS_METRIC_WEATHER_TEMPERATURE_AIR_2M_C;
    rec.value_type = SS_SDK_VALUE_F64;
    rec.value.f64 = value;
    rec.ts_start_utc = ts_start;
    rec.ts_end_utc = ts_start + 900;
    rec.data_kind = SS_SDK_DATA_OBSERVATION;
    return rec;
}

bool seed_db_rows(int rows, int64_t *out_start)
{
    int64_t base;

    if (!reset_db_file()) {
        return false;
    }

    base = align_slot((int64_t)time(NULL)) - (int64_t)rows * 900;
    if (out_start != NULL) {
        *out_start = base;
    }

    for (int i = 0; i < rows; ++i) {
        const ss_sdk_record rec = make_record(base + (int64_t)i * 900, 10.0 + (double)(i % 20));
        if (ss_sdk_db_write_record(&rec) != SS_SDK_OK) {
            return false;
        }
    }

    return true;
}

}  // namespace

static void BM_SdkDbWriteSingle(benchmark::State &state)
{
    if (!ensure_bench_db_path()) {
        state.SkipWithError("failed to create benchmark DB path");
        return;
    }

    int seq = 0;
    for (auto _ : state) {
        const int64_t now_slot = align_slot((int64_t)time(NULL));
        const ss_sdk_record rec = make_record(now_slot + (int64_t)seq * 900, 7.5 + (double)(seq % 5));
        ++seq;

        if (!reset_db_file()) {
            state.SkipWithError("failed to reset benchmark DB");
            return;
        }

        const ss_sdk_status rc = ss_sdk_db_write_record(&rec);
        if (rc != SS_SDK_OK) {
            state.SkipWithError("ss_sdk_db_write_record failed");
            return;
        }
        benchmark::DoNotOptimize(rc);
    }
}
BENCHMARK(BM_SdkDbWriteSingle);  // NOLINT(cert-err58-cpp)

static void BM_SdkDbWriteDuplicate(benchmark::State &state)
{
    if (!ensure_bench_db_path()) {
        state.SkipWithError("failed to create benchmark DB path");
        return;
    }

    const int64_t now_slot = align_slot((int64_t)time(NULL));
    const ss_sdk_record rec = make_record(now_slot, 14.0);

    for (auto _ : state) {
        if (!reset_db_file()) {
            state.SkipWithError("failed to reset benchmark DB");
            return;
        }
        if (ss_sdk_db_write_record(&rec) != SS_SDK_OK) {
            state.SkipWithError("failed to seed duplicate row");
            return;
        }

        const ss_sdk_status rc = ss_sdk_db_write_record(&rec);
        if (rc != SS_SDK_OK) {
            state.SkipWithError("duplicate write failed");
            return;
        }
        benchmark::DoNotOptimize(rc);
    }
}
BENCHMARK(BM_SdkDbWriteDuplicate);  // NOLINT(cert-err58-cpp)

static void BM_SdkDbReadCanonical(benchmark::State &state)
{
    int64_t start_utc = 0;
    if (!seed_db_rows(256, &start_utc)) {
        state.SkipWithError("failed to seed benchmark DB rows");
        return;
    }

    for (auto _ : state) {
        ss_sdk_samples_out out = {NULL, 0};
        const ss_sdk_status rc = ss_sdk_db_get_canonical(
            start_utc,
            256,
            SS_METRIC_WEATHER_TEMPERATURE_AIR_2M_C,
            &out);
        if (rc != SS_SDK_OK) {
            state.SkipWithError("ss_sdk_db_get_canonical failed");
            return;
        }
        benchmark::DoNotOptimize(out.count);
        ss_sdk_db_free_samples(&out);
    }
}
BENCHMARK(BM_SdkDbReadCanonical);  // NOLINT(cert-err58-cpp)

BENCHMARK_MAIN();
