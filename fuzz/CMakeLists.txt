if(NOT SUNSPOTS_FUZZ_ENGINE STREQUAL "afl")
  message(FATAL_ERROR "SUNSPOTS_FUZZ_ENGINE must be 'afl'")
endif()

function(sunspots_add_fuzzer target_name source_file)
  set(source_path "${CMAKE_CURRENT_SOURCE_DIR}/${source_file}")
  if(NOT EXISTS "${source_path}")
    message(STATUS "Skipping fuzzer ${target_name}: missing ${source_file}")
    return()
  endif()

  add_executable(${target_name}
    ${source_path}
  )
  target_compile_features(${target_name} PRIVATE cxx_std_11)
  sunspots_apply_target_defaults(${target_name})

  if(SUNSPOTS_FUZZ_ENGINE STREQUAL "afl")
    target_compile_definitions(${target_name} PRIVATE SUNSPOTS_FUZZ_ENGINE_AFL=1)
    target_compile_options(${target_name} PRIVATE
      -O1
      -g
      -fno-omit-frame-pointer
    )
  endif()
endfunction()

sunspots_add_fuzzer(http_request_fuzzer http_request_fuzzer.cpp)
if(TARGET http_request_fuzzer)
  target_link_libraries(http_request_fuzzer
    PRIVATE
      sunspots_frontend_core
  )
endif()

sunspots_add_fuzzer(openmeteo_transform_fuzzer openmeteo_transform_fuzzer.cpp)
if(TARGET openmeteo_transform_fuzzer)
  target_link_libraries(openmeteo_transform_fuzzer
    PRIVATE
      sunspots_weather_transform
  )
endif()

sunspots_add_fuzzer(config_args_fuzzer config_args_fuzzer.cpp)
if(TARGET config_args_fuzzer)
  target_link_libraries(config_args_fuzzer
    PRIVATE
      sunspots_config
  )
endif()

sunspots_add_fuzzer(compute_calculator_fuzzer compute_calculator_fuzzer.cpp)
if(TARGET compute_calculator_fuzzer)
  target_link_libraries(compute_calculator_fuzzer
    PRIVATE
      sunspots_compute
  )
endif()

sunspots_add_fuzzer(sdk_db_fuzzer sdk_db_fuzzer.cpp)
if(TARGET sdk_db_fuzzer)
  target_link_libraries(sdk_db_fuzzer
    PRIVATE
      sunspots_sdk
  )
endif()

set(fetch_manager_source "${CMAKE_SOURCE_DIR}/src/fetch/fetch_manager.c")
if(EXISTS "${fetch_manager_source}")
  add_library(sunspots_fetch_manager_fuzz_obj OBJECT
    ${fetch_manager_source}
  )
  target_compile_definitions(sunspots_fetch_manager_fuzz_obj PRIVATE SUNSPOTS_FETCH_MANAGER_NO_MAIN=1)
  target_include_directories(sunspots_fetch_manager_fuzz_obj
    PRIVATE
      ${CMAKE_SOURCE_DIR}/src/fetch
  )
  target_link_libraries(sunspots_fetch_manager_fuzz_obj
    PRIVATE
      sunspots_curly
      sunspots_cjson
      Threads::Threads
  )
  target_compile_features(sunspots_fetch_manager_fuzz_obj PRIVATE c_std_99)
  sunspots_apply_target_defaults(sunspots_fetch_manager_fuzz_obj)
else()
  message(STATUS "Skipping fetch manager fuzz object: missing src/fetch/fetch_manager.c")
endif()

sunspots_add_fuzzer(fetch_manager_config_fuzzer fetch_manager_config_fuzzer.cpp)
if(TARGET fetch_manager_config_fuzzer AND TARGET sunspots_fetch_manager_fuzz_obj)
  target_link_libraries(fetch_manager_config_fuzzer
    PRIVATE
      sunspots_curly
      sunspots_cjson
      Threads::Threads
      $<TARGET_OBJECTS:sunspots_fetch_manager_fuzz_obj>
  )
endif()
