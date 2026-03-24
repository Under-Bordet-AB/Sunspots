add_library(sunspots_compute STATIC
  algorithms/compute_heuristic.c
  algorithms/compute_lp.c
)
target_include_directories(sunspots_compute
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/algorithms
)
target_link_libraries(sunspots_compute
  PRIVATE
    GLPK::GLPK
)
target_compile_features(sunspots_compute PUBLIC c_std_99)
sunspots_apply_target_defaults(sunspots_compute)

add_executable(sunspots_compute_manager
  compute_manager.c
)
target_link_libraries(sunspots_compute_manager
  PRIVATE
    sunspots_compute
    sunspots_cjson
    sunspots_sdk
    Threads::Threads
)
target_compile_features(sunspots_compute_manager PRIVATE c_std_99)
sunspots_apply_target_defaults(sunspots_compute_manager)
