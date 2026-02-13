add_library(sunspots_cjson STATIC
  json/cJSON.c
)
target_include_directories(sunspots_cjson
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/json
)
target_compile_features(sunspots_cjson PUBLIC c_std_99)
sunspots_apply_target_defaults(sunspots_cjson)

add_library(sunspots_linked_list STATIC
  linked_list/linked_list.c
)
target_include_directories(sunspots_linked_list
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/linked_list
)
target_compile_features(sunspots_linked_list PUBLIC c_std_99)
sunspots_apply_target_defaults(sunspots_linked_list)

add_library(sunspots_curly STATIC
  curly.c
)
target_include_directories(sunspots_curly
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)
target_link_libraries(sunspots_curly
  PUBLIC
    CURL::libcurl
)
target_compile_features(sunspots_curly PUBLIC c_std_99)
sunspots_apply_target_defaults(sunspots_curly)
