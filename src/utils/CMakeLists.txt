add_library(sunspots_utils STATIC
  unit_utils.c
)
target_include_directories(sunspots_utils
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)
target_compile_features(sunspots_utils PUBLIC c_std_99)
sunspots_apply_target_defaults(sunspots_utils)