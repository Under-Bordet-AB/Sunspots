add_library(sunspots_cjson STATIC
  json/cJSON.c
)
target_include_directories(sunspots_cjson
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/json
)
target_compile_features(sunspots_cjson PUBLIC c_std_99)
sunspots_apply_target_defaults(sunspots_cjson)
if(SUNSPOTS_ENABLE_CLANG_TIDY)
  # cJSON is vendored third-party code; keep it out of project clang-tidy enforcement.
  set_target_properties(sunspots_cjson PROPERTIES
    C_CLANG_TIDY ""
    CXX_CLANG_TIDY ""
  )
endif()

add_library(sunspots_linked_list STATIC
  linked_list/linked_list.c
)
target_include_directories(sunspots_linked_list
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/linked_list
)
target_compile_features(sunspots_linked_list PUBLIC c_std_99)
sunspots_apply_target_defaults(sunspots_linked_list)
