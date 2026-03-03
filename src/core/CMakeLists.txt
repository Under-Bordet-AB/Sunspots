function(sunspots_add_daemon_target target_name)
  add_executable(${target_name}
    main.c
    daemon.c
    module.c
  )
  target_include_directories(${target_name}
    PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}
  )
  target_link_libraries(${target_name}
    PRIVATE
      sunspots_cjson
  )
  target_compile_features(${target_name} PRIVATE c_std_99)
  target_compile_options(${target_name} PRIVATE -Wformat=0)
  sunspots_apply_target_defaults(${target_name})
endfunction()

sunspots_add_daemon_target(sunspots_daemon)
sunspots_add_daemon_target(sunspots_daemon_dummy)
target_compile_definitions(sunspots_daemon_dummy PRIVATE SUNSPOTS_DAEMON_FORCE_FOREGROUND=1)
# target_compile_definitions(sunspots_daemon PRIVATE DEBUG)

add_executable(sunspots_logger
    daemon_logger.c
)
target_link_libraries(sunspots_logger
  PRIVATE
    sunspots_cjson
)
target_compile_definitions(sunspots_logger PRIVATE RING_BUF_4)
target_compile_features(sunspots_logger PRIVATE c_std_99)
target_compile_options(sunspots_logger PRIVATE -Wformat=0)
sunspots_apply_target_defaults(sunspots_logger)
