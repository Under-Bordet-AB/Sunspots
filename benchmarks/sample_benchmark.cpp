#include <benchmark/benchmark.h>

static void BM_SampleCalculateSimple(benchmark::State& state) {
  double input = 650.0;
  double output = 0.0;

  for (auto _ : state) {
    output = input * 0.82 + 7.5;
    benchmark::DoNotOptimize(output);
    benchmark::ClobberMemory();
  }
}

BENCHMARK(BM_SampleCalculateSimple);  // NOLINT(cert-err58-cpp)
BENCHMARK_MAIN();
