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
    )
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)
  endif()

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
