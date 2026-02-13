#include <benchmark/benchmark.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>

#include <fcntl.h>
#include <unistd.h>

extern "C" {
#include "sdk/ss_sdk.h"
}

namespace {

std::string g_db_dir;
std::string g_db_path;

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
    g_db_path = g_db_dir + "/sdk_bench.tsv";
    if (setenv("SS_SDK_DB_PATH", g_db_path.c_str(), 1) != 0) {
        return false;
    }

    return true;
}

bool truncate_db_file()
{
    if (!ensure_bench_db_path()) {
        return false;
    }

    const int fd = open(g_db_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0664);
    if (fd < 0) {
        return false;
    }
    close(fd);
    return true;
}

ss_sdk_record make_record(int64_t ts_start, double value, const char *source_field)
{
    ss_sdk_record rec;
    std::memset(&rec, 0, sizeof(rec));
    rec.metric = SS_METRIC_WEATHER_TEMPERATURE_AIR_2M_C;
    rec.value_type = SS_SDK_VALUE_F64;
    rec.value.f64 = value;
    rec.ts_start_utc = ts_start;
    rec.ts_end_utc = ts_start + 60;
    rec.data_kind = SS_SDK_DATA_OBSERVATION;
    rec.source_api = "benchmark";
    rec.source_field = source_field;
    rec.source_tz = "UTC";
    rec.model_id = "";
    return rec;
}

bool seed_db_rows(int rows)
{
    if (!truncate_db_file()) {
        return false;
    }

    const int64_t base = (int64_t)time(NULL) - 3600;
    for (int i = 0; i < rows; ++i) {
        const ss_sdk_record rec = make_record(base + i * 60, 10.0 + (double)(i % 20), "temperature_2m");
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
        state.PauseTiming();
        if (!truncate_db_file()) {
            state.SkipWithError("failed to truncate benchmark DB");
            return;
        }
        const int64_t now = (int64_t)time(NULL);
        const ss_sdk_record rec = make_record(now + seq, 7.5 + (double)(seq % 5), "temperature_2m");
        ++seq;
        state.ResumeTiming();

        const ss_sdk_status rc = ss_sdk_db_write_record(&rec);
        if (rc != SS_SDK_OK) {
            state.SkipWithError("ss_sdk_db_write_record failed");
            return;
        }
        benchmark::DoNotOptimize(rc);
    }
}
BENCHMARK(BM_SdkDbWriteSingle);

static void BM_SdkDbWriteDuplicate(benchmark::State &state)
{
    if (!ensure_bench_db_path()) {
        state.SkipWithError("failed to create benchmark DB path");
        return;
    }

    const int64_t now = (int64_t)time(NULL);
    const ss_sdk_record rec = make_record(now, 14.0, "temperature_2m");

    for (auto _ : state) {
        state.PauseTiming();
        if (!truncate_db_file()) {
            state.SkipWithError("failed to truncate benchmark DB");
            return;
        }
        if (ss_sdk_db_write_record(&rec) != SS_SDK_OK) {
            state.SkipWithError("failed to seed duplicate row");
            return;
        }
        state.ResumeTiming();

        const ss_sdk_status rc = ss_sdk_db_write_record(&rec);
        if (rc != SS_SDK_OK) {
            state.SkipWithError("duplicate write failed");
            return;
        }
        benchmark::DoNotOptimize(rc);
    }
}
BENCHMARK(BM_SdkDbWriteDuplicate);

static void BM_SdkDbReadLastWeeks(benchmark::State &state)
{
    if (!seed_db_rows(256)) {
        state.SkipWithError("failed to seed benchmark DB rows");
        return;
    }

    for (auto _ : state) {
        ss_sdk_record *rows = NULL;
        size_t count = 0;
        const ss_sdk_status rc = ss_sdk_db_get_last_weeks(8, &rows, &count);
        if (rc != SS_SDK_OK) {
            state.SkipWithError("ss_sdk_db_get_last_weeks failed");
            return;
        }
        benchmark::DoNotOptimize(count);
        ss_sdk_db_free_records(rows);
    }
}
BENCHMARK(BM_SdkDbReadLastWeeks);

BENCHMARK_MAIN();
