add_executable(sunspots_client
  main.cpp
  http/http_client.cpp
  http/http_request.cpp
  http/http_response.cpp
  menu/menu.cpp
  utils/input.cpp
  utils/utils.cpp
  plan_service/plan_service.cpp
)

target_include_directories(sunspots_client
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src/libs/json
)

target_link_libraries(sunspots_client
  PRIVATE
    sunspots_cjson
)

target_compile_features(sunspots_client PRIVATE cxx_std_11)
sunspots_apply_target_defaults(sunspots_client)
