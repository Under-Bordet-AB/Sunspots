cmake_minimum_required(VERSION 3.22)

project(Sunspots VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(CTest)
include(SunspotsOptions)
include(SunspotsDependencies)

add_subdirectory(src)

if(BUILD_TESTING)
  add_subdirectory(tests)
endif()

if(SUNSPOTS_BUILD_BENCHMARKS)
  add_subdirectory(benchmarks)
endif()

if(SUNSPOTS_BUILD_FUZZERS)
  add_subdirectory(fuzz)
endif()

if(BUILD_TESTING)
  add_custom_target(test-all
    COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure
    USES_TERMINAL
  )

  add_custom_target(test-unit
    COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure -L unit
    USES_TERMINAL
  )

  add_custom_target(test-integration
    COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure -L integration
    USES_TERMINAL
  )
endif()

if(SUNSPOTS_BUILD_BENCHMARKS)
  add_custom_target(bench
    COMMAND $<TARGET_FILE:sample_benchmark> --benchmark_min_time=0.01 --benchmark_repetitions=1
    DEPENDS sample_benchmark
    USES_TERMINAL
  )
endif()

if(SUNSPOTS_ENABLE_VALGRIND AND VALGRIND_EXECUTABLE)
  add_custom_target(valgrind
    COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure -L valgrind
    USES_TERMINAL
  )
endif()
