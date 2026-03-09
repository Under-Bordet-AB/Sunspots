add_executable(sunspots_backfill_openmeteo
  backfill_openmeteo.c
)
target_link_libraries(sunspots_backfill_openmeteo
  PRIVATE
    sunspots_sdk
    CURL::libcurl
    sunspots_cjson
    Threads::Threads
)
target_compile_features(sunspots_backfill_openmeteo PRIVATE c_std_99)
sunspots_apply_target_defaults(sunspots_backfill_openmeteo)
