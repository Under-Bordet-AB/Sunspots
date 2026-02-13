add_library(sunspots_config STATIC
  config.c
)
target_include_directories(sunspots_config
  PUBLIC
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}
)
target_link_libraries(sunspots_config
  PUBLIC
    sunspots_cjson
)
target_compile_features(sunspots_config PUBLIC c_std_99)
sunspots_apply_target_defaults(sunspots_config)
