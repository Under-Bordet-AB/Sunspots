function(sunspots_add_fetch_api target_name source_file)
  add_executable(${target_name}
    ${source_file}
  )
  target_link_libraries(${target_name}
    PRIVATE
      CURL::libcurl
      sunspots_cjson
      sunspots_sdk
      sunspots_weather_transform
      sunspots_price_transform
      Threads::Threads
  )
  target_compile_features(${target_name} PRIVATE c_std_99)
  sunspots_apply_target_defaults(${target_name})
endfunction()

sunspots_add_fetch_api(sunspots_fetch_openmeteo fetch_openmeteo.c)
sunspots_add_fetch_api(sunspots_fetch_openmeteo_forecast fetch_openmeteo_forecast.c)
sunspots_add_fetch_api(sunspots_fetch_elprisjustnu fetch_elprisjustnu.c)
