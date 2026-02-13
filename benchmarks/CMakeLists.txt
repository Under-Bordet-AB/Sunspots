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

add_executable(sdk_db_benchmark
  sdk_db_benchmark.cpp
)
target_link_libraries(sdk_db_benchmark
  PRIVATE
    benchmark::benchmark
    sunspots_sdk
)
target_compile_features(sdk_db_benchmark PRIVATE cxx_std_11)
sunspots_apply_target_defaults(sdk_db_benchmark)
