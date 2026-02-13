add_library(sunspots_sdk STATIC
  ss_sdk.c
  ss_canonical.c
  internal/db/ss_db_internal.c
  internal/log/ss_log_internal.c
)
target_include_directories(sunspots_sdk
  PUBLIC
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}
)
target_compile_features(sunspots_sdk PUBLIC c_std_99)
sunspots_apply_target_defaults(sunspots_sdk)
