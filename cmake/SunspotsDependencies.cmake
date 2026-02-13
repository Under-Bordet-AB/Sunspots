include(FetchContent)

find_package(Threads REQUIRED)
find_package(CURL REQUIRED)

if(BUILD_TESTING)
  find_package(GTest CONFIG QUIET)
  if(NOT TARGET GTest::gtest)
    message(STATUS "GTest not found via package manager; fetching release-1.12.1")
    FetchContent_Declare(
      googletest
      URL https://github.com/google/googletest/archive/refs/tags/release-1.12.1.tar.gz
      URL_HASH SHA256=81964fe578e9bd7c94dfdb09c8e4d6e6759e19967e397dbea48d1c10e45d0df2
    )
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)
  endif()

endif()

if(SUNSPOTS_BUILD_BENCHMARKS)
  find_package(benchmark CONFIG QUIET)
  if(NOT TARGET benchmark::benchmark)
    message(STATUS "Google Benchmark not found via package manager; fetching v1.7.1")
    set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
    set(BENCHMARK_ENABLE_GTEST_TESTS OFF CACHE BOOL "" FORCE)
    set(BENCHMARK_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
    set(BENCHMARK_DOWNLOAD_DEPENDENCIES OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(
      benchmark
      URL https://github.com/google/benchmark/archive/refs/tags/v1.7.1.tar.gz
      URL_HASH SHA256=6430e4092653380d9dc4ccb45a1e2dc9259d581f4866dc0759713126056bc1d7
    )
    FetchContent_MakeAvailable(benchmark)
  endif()
endif()

if(SUNSPOTS_ENABLE_VALGRIND)
  find_program(VALGRIND_EXECUTABLE NAMES valgrind)
  if(NOT VALGRIND_EXECUTABLE)
    message(STATUS "Valgrind not found. valgrind test targets will be skipped.")
  endif()
endif()
