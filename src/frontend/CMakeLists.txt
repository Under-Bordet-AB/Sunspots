add_library(sunspots_frontend_core STATIC
  client_queue.c
  endpoints.c
  http_main.c
  http_parser.c
  http_worker.c
)
target_include_directories(sunspots_frontend_core
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src/libs/linked_list
    ${CMAKE_SOURCE_DIR}/src/libs/json
)
target_link_libraries(sunspots_frontend_core
  PUBLIC
    sunspots_linked_list
    sunspots_cjson
    Threads::Threads
)
target_compile_features(sunspots_frontend_core PUBLIC c_std_99)
sunspots_apply_target_defaults(sunspots_frontend_core)

add_executable(sunspots_frontend
  frontend_main.c
)
target_link_libraries(sunspots_frontend
  PRIVATE
    sunspots_frontend_core
)
target_compile_features(sunspots_frontend PRIVATE c_std_99)
sunspots_apply_target_defaults(sunspots_frontend)
