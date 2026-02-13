if(NOT TARGET benchmark::benchmark)
  message(FATAL_ERROR "benchmark::benchmark target is required when SUNSPOTS_BUILD_BENCHMARKS=ON")
endif()

add_executable(sample_benchmark
  sample_benchmark.cpp
)
target_link_libraries(sample_benchmark
  PRIVATE
    benchmark::benchmark
    sunspots_compute
)
target_compile_features(sample_benchmark PRIVATE cxx_std_11)
sunspots_apply_target_defaults(sample_benchmark)
