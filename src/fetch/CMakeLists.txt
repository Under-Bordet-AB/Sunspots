add_executable(sunspots_fetch_manager
  fetch_manager.c
)
target_include_directories(sunspots_fetch_manager
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)
target_link_libraries(sunspots_fetch_manager
  PRIVATE
    sunspots_curly
    sunspots_cjson
    Threads::Threads
)
target_compile_features(sunspots_fetch_manager PRIVATE c_std_99)
sunspots_apply_target_defaults(sunspots_fetch_manager)

add_subdirectory(apis)
