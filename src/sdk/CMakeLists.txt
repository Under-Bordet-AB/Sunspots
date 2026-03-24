find_package(SQLite3 REQUIRED)

add_library(sunspots_sdk STATIC
  ss_sdk.c
  ss_canonical.c
  internal/ss_sdk_config.c
  internal/ss_sdk_config_util.c
  internal/db/ss_db_internal.c
  internal/db/ss_db_internal_read.c
  internal/log/ss_log_internal.c
)
target_link_libraries(sunspots_sdk
  PUBLIC
    sunspots_cjson
  PRIVATE
    SQLite::SQLite3
)
target_include_directories(sunspots_sdk
  PUBLIC
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}
)
target_compile_features(sunspots_sdk PUBLIC c_std_99)
if(BUILD_TESTING)
  target_compile_definitions(sunspots_sdk PUBLIC SS_SDK_ENABLE_TEST_HOOKS=1)
endif()
sunspots_apply_target_defaults(sunspots_sdk)

add_library(sunspots_sdk_shared SHARED
  ${CMAKE_SOURCE_DIR}/src/libs/json/cJSON.c
  ss_sdk.c
  ss_canonical.c
  internal/ss_sdk_config.c
  internal/ss_sdk_config_util.c
  internal/db/ss_db_internal.c
  internal/db/ss_db_internal_read.c
  internal/log/ss_log_internal.c
)
target_link_libraries(sunspots_sdk_shared
  PRIVATE
    SQLite::SQLite3
)
target_include_directories(sunspots_sdk_shared
  PUBLIC
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src/libs/json
)
target_compile_features(sunspots_sdk_shared PUBLIC c_std_99)
if(BUILD_TESTING)
  target_compile_definitions(sunspots_sdk_shared PUBLIC SS_SDK_ENABLE_TEST_HOOKS=1)
endif()
set_target_properties(sunspots_sdk_shared PROPERTIES OUTPUT_NAME sunspots_sdk_shared)
target_compile_definitions(sunspots_sdk_shared PRIVATE _POSIX_C_SOURCE=200809L)
if(MSVC)
  target_compile_options(sunspots_sdk_shared PRIVATE /W3)
else()
  target_compile_options(sunspots_sdk_shared PRIVATE -Wall -Wextra -Wpedantic)
endif()
