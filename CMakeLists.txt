cmake_minimum_required(VERSION 3.22)

project(Sunspots LANGUAGES C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/build/bin)

find_package(Threads REQUIRED)
find_package(CURL REQUIRED)

# Core runtime SDK: config/bootstrap/runtime-supervision primitives.
add_library(ss_core STATIC
    src/config/config.c
    src/core/config_runtime.c
    src/core/runtime_supervision.c
    src/core/worker_bootstrap.c
    src/libs/json/cJSON.c
)

target_include_directories(ss_core PUBLIC src .)
target_compile_options(ss_core PRIVATE -Wall -Wextra)
target_compile_definitions(ss_core PRIVATE _GNU_SOURCE _POSIX_C_SOURCE=200809L)
target_link_libraries(ss_core PUBLIC Threads::Threads)

# Fetch SDK: generic fetch engine + provider dispatch/mapping.
add_library(ss_fetch STATIC
    src/fetch/fetch_engine.c
)
target_include_directories(ss_fetch PUBLIC src .)
target_compile_options(ss_fetch PRIVATE -Wall -Wextra)
target_compile_definitions(ss_fetch PRIVATE _GNU_SOURCE _POSIX_C_SOURCE=200809L)
target_link_libraries(ss_fetch PUBLIC ss_core ss_sdk CURL::libcurl Threads::Threads)

# Canonical SDK: validation, persistence, and logging.
add_library(ss_sdk STATIC
    src/sdk/ss_sdk.c
    src/sdk/ss_canonical.c
    src/sdk/internal/db/ss_db_internal.c
    src/sdk/internal/log/ss_log_internal.c
)

target_include_directories(ss_sdk PUBLIC src .)
target_compile_options(ss_sdk PRIVATE -Wall -Wextra)
target_compile_definitions(ss_sdk PRIVATE _GNU_SOURCE _POSIX_C_SOURCE=200809L)
target_link_libraries(ss_sdk PUBLIC Threads::Threads m)

add_executable(sunspotsd src/core/sunspotsd.c)
target_compile_options(sunspotsd PRIVATE -Wall -Wextra)
target_compile_definitions(sunspotsd PRIVATE _GNU_SOURCE _POSIX_C_SOURCE=200809L)
target_link_libraries(sunspotsd PRIVATE ss_core ss_sdk)

add_executable(fetch_worker src/workers/fetch_worker_main.c)
target_compile_options(fetch_worker PRIVATE -Wall -Wextra)
target_compile_definitions(fetch_worker PRIVATE _GNU_SOURCE _POSIX_C_SOURCE=200809L)
target_link_libraries(fetch_worker PRIVATE ss_fetch)

add_executable(calc_smhi_avg_temp src/workers/calc_smhi_avg_temp.c)
target_compile_options(calc_smhi_avg_temp PRIVATE -Wall -Wextra)
target_compile_definitions(calc_smhi_avg_temp PRIVATE _GNU_SOURCE _POSIX_C_SOURCE=200809L)
target_link_libraries(calc_smhi_avg_temp PRIVATE ss_core ss_sdk Threads::Threads)

add_executable(sunspots_server src/server/sunspots_server.c)
target_compile_options(sunspots_server PRIVATE -Wall -Wextra)
target_compile_definitions(sunspots_server PRIVATE _GNU_SOURCE _POSIX_C_SOURCE=200809L)
target_link_libraries(sunspots_server PRIVATE ss_core ss_sdk Threads::Threads)
