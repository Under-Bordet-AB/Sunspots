include(FetchContent)

find_package(Threads REQUIRED)
find_package(CURL REQUIRED)

find_package(GLPK QUIET)
if(NOT TARGET GLPK::GLPK)
  find_package(PkgConfig QUIET)
  if(PkgConfig_FOUND)
    pkg_check_modules(PC_GLPK QUIET glpk)
  endif()

  find_path(SUNSPOTS_GLPK_INCLUDE_DIR
    NAMES glpk.h
    HINTS ${PC_GLPK_INCLUDEDIR} ${PC_GLPK_INCLUDE_DIRS}
  )
  find_library(SUNSPOTS_GLPK_LIBRARY
    NAMES glpk
    HINTS ${PC_GLPK_LIBDIR} ${PC_GLPK_LIBRARY_DIRS}
  )

  if(SUNSPOTS_GLPK_INCLUDE_DIR AND SUNSPOTS_GLPK_LIBRARY)
    add_library(GLPK::GLPK UNKNOWN IMPORTED)
    set_target_properties(GLPK::GLPK PROPERTIES
      IMPORTED_LOCATION ${SUNSPOTS_GLPK_LIBRARY}
      INTERFACE_INCLUDE_DIRECTORIES ${SUNSPOTS_GLPK_INCLUDE_DIR}
    )
  else()
    message(FATAL_ERROR "GLPK not found. Install development headers/libraries (e.g. libglpk-dev) or provide a package config.")
  endif()
endif()

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
