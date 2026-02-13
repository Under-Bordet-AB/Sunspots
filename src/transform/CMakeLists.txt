add_library(sunspots_weather_transform STATIC
  weather/openmeteo.c
  weather/weather_model.c
)
target_include_directories(sunspots_weather_transform
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/weather
    ${CMAKE_SOURCE_DIR}/src/utils
    ${CMAKE_SOURCE_DIR}/src/libs/json
)
target_link_libraries(sunspots_weather_transform
  PUBLIC
    sunspots_cjson
    sunspots_utils
)
target_compile_features(sunspots_weather_transform PUBLIC c_std_99)
sunspots_apply_target_defaults(sunspots_weather_transform)
