cmake_minimum_required(VERSION 3.22)

project(Sunspots LANGUAGES C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Keep binaries in repo-local `bin/` for a simple workflow.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/bin")

option(SS_ENABLE_SANITIZERS "Enable ASAN+UBSAN (Debug-oriented)." OFF)
option(SS_BUILD_EXPERIMENTAL "Build experimental/scratch binaries." OFF)

include(CheckCCompilerFlag)

add_library(ss_build_options INTERFACE)
target_compile_definitions(ss_build_options INTERFACE _GNU_SOURCE _POSIX_C_SOURCE=200809L)
target_compile_options(ss_build_options INTERFACE -Wall -Wextra)
target_include_directories(ss_build_options INTERFACE
  "${CMAKE_SOURCE_DIR}/src"
  "${CMAKE_SOURCE_DIR}/src/libs"
)

find_package(Threads REQUIRED)

if(SS_ENABLE_SANITIZERS)
  # ASAN/UBSAN: great dev feedback. Not for production.
  target_compile_options(ss_build_options INTERFACE -fsanitize=address,undefined -fno-omit-frame-pointer)
  target_link_options(ss_build_options INTERFACE -fsanitize=address,undefined)
endif()

# ---------------------------------------------------------------------------
# Third-party / vendored libs
# ---------------------------------------------------------------------------

add_library(cjson STATIC
  src/libs/json/cJSON.c
)
target_include_directories(cjson PUBLIC src/libs/json)
target_link_libraries(cjson PUBLIC ss_build_options)

add_library(curly STATIC
  src/libs/curly.c
)
target_include_directories(curly PUBLIC src)
target_link_libraries(curly PUBLIC ss_build_options Threads::Threads)

find_package(CURL)
if(CURL_FOUND)
  target_link_libraries(curly PUBLIC CURL::libcurl)
endif()

# ---------------------------------------------------------------------------
# SDK (public integration surface)
# ---------------------------------------------------------------------------

add_library(ss_sdk STATIC
  src/sdk/ss_sdk.c
  src/sdk/ss_canonical.c
  src/sdk/internal/db/ss_db_internal.c
  src/sdk/internal/log/ss_log_internal.c
)
target_include_directories(ss_sdk PUBLIC src)
target_link_libraries(ss_sdk PUBLIC ss_build_options Threads::Threads)

# ---------------------------------------------------------------------------
# Executables
# ---------------------------------------------------------------------------

# Daemon supervisor: parses config and spawns children.
add_executable(sunspots_daemon
  src/core/main.c
)
target_link_libraries(sunspots_daemon PRIVATE ss_build_options Threads::Threads cjson)

# Standalone HTTP server (work-in-progress).
add_executable(sunspots_server
  src/server/server.c
)
target_link_libraries(sunspots_server PRIVATE ss_build_options Threads::Threads)

# Compute manager (work-in-progress).
add_executable(compute_manager
  src/compute/compute_manager.c
  src/compute/compute.c
)
target_link_libraries(compute_manager PRIVATE ss_build_options Threads::Threads cjson m)

# Fetch manager (spawns API workers).
add_executable(fetch_manager
  src/fetch/fetch_manager.c
)
target_link_libraries(fetch_manager PRIVATE ss_build_options Threads::Threads cjson)
target_include_directories(fetch_manager PRIVATE src/fetch)

if(CURL_FOUND)
  target_link_libraries(fetch_manager PRIVATE curly)
endif()

if(CURL_FOUND)
  # API fetcher workers (require libcurl).
  add_executable(fetch_openmeteo
    src/fetch/apis/fetch_openmeteo.c
  )
  target_link_libraries(fetch_openmeteo PRIVATE ss_build_options Threads::Threads curly cjson)
  target_include_directories(fetch_openmeteo PRIVATE src/fetch)

  add_executable(fetch_elprisjustnu
    src/fetch/apis/fetch_elprisjustnu.c
  )
  target_link_libraries(fetch_elprisjustnu PRIVATE ss_build_options Threads::Threads curly cjson)
  target_include_directories(fetch_elprisjustnu PRIVATE src/fetch)
else()
  message(STATUS "libcurl not found: skipping fetcher workers (fetch_openmeteo, fetch_elprisjustnu).")
endif()

if(SS_BUILD_EXPERIMENTAL)
  # Various scratch/test binaries in the tree. They may assume files/paths that
  # are not present in all environments.
  add_executable(core_fetch_data src/core/fetch_data.c)
  target_link_libraries(core_fetch_data PRIVATE ss_build_options Threads::Threads)

  add_executable(fetch_data src/fetch/fetch_data.c)
  target_link_libraries(fetch_data PRIVATE ss_build_options Threads::Threads)
endif()
