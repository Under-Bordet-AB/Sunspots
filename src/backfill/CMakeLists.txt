add_library(sunspots_backfill_internal STATIC
  backfill_common.c
  backfill_config.c
  backfill_holes.c
  backfill_payload.c
)
target_link_libraries(sunspots_backfill_internal
  PRIVATE
    sunspots_sdk
    sunspots_curly
    sunspots_cjson
    Threads::Threads
)
target_compile_features(sunspots_backfill_internal PRIVATE c_std_99)
sunspots_apply_target_defaults(sunspots_backfill_internal)

add_executable(sunspots_backfill_openmeteo
  backfill_openmeteo.c
)
target_link_libraries(sunspots_backfill_openmeteo
  PRIVATE
    sunspots_backfill_internal
    sunspots_sdk
    sunspots_curly
    sunspots_cjson
    Threads::Threads
)
target_compile_features(sunspots_backfill_openmeteo PRIVATE c_std_99)
sunspots_apply_target_defaults(sunspots_backfill_openmeteo)

set_target_properties(sunspots_backfill_openmeteo PROPERTIES OUTPUT_NAME backfill)

add_custom_command(TARGET sunspots_backfill_openmeteo POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:sunspots_backfill_openmeteo> ${CMAKE_SOURCE_DIR}/backfill
)
