#include <benchmark/benchmark.h>

extern "C" {
#include "compute.h"
}

static void BM_SampleCalculateSimple(benchmark::State& state) {
  data_t in = {};
  result_t out = {};

  in.irradiance = 650.0;
  in.cloudiness = 0.3;
  in.temperature = 8.0;
  in.spot_price = 1.2;
  in.battery_charge = 55.0;

  for (auto _ : state) {
    benchmark::DoNotOptimize(calculate_simple(&in, &out));
    benchmark::ClobberMemory();
  }
}

BENCHMARK(BM_SampleCalculateSimple);
BENCHMARK_MAIN();
