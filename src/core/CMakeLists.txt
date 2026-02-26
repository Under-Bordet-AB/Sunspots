add_executable(sunspots_daemon
  main.c
  daemon.c
  module.c
)
target_include_directories(sunspots_daemon
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)
target_link_libraries(sunspots_daemon
  PRIVATE
    sunspots_cjson
)
# target_compile_definitions(sunspots_daemon PRIVATE DEBUG)
target_compile_features(sunspots_daemon PRIVATE c_std_99)
target_compile_options(sunspots_daemon PRIVATE -Wformat=0)
sunspots_apply_target_defaults(sunspots_daemon)

add_executable(sunspots_logger
    daemon_logger.c
)
target_link_libraries(sunspots_logger
  PRIVATE
    sunspots_cjson
)
target_compile_features(sunspots_logger PRIVATE c_std_99)
target_compile_options(sunspots_logger PRIVATE -Wformat=0)
sunspots_apply_target_defaults(sunspots_logger)
