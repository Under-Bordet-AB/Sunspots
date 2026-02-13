add_executable(sunspots_daemon
  main.c
)
target_include_directories(sunspots_daemon
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)
target_link_libraries(sunspots_daemon
  PRIVATE
    sunspots_cjson
)
target_compile_features(sunspots_daemon PRIVATE c_std_99)
sunspots_apply_target_defaults(sunspots_daemon)
