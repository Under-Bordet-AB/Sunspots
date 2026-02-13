if(SUNSPOTS_FUZZ_ENGINE STREQUAL "libfuzzer")
  if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    message(FATAL_ERROR "SUNSPOTS_FUZZ_ENGINE=libfuzzer requires clang/clang++")
  endif()
elseif(NOT SUNSPOTS_FUZZ_ENGINE STREQUAL "afl")
  message(FATAL_ERROR "SUNSPOTS_FUZZ_ENGINE must be one of: libfuzzer, afl")
endif()

function(sunspots_add_fuzzer target_name source_file)
  add_executable(${target_name}
    ${source_file}
  )
  target_compile_features(${target_name} PRIVATE cxx_std_11)
  sunspots_apply_target_defaults(${target_name})

  if(SUNSPOTS_FUZZ_ENGINE STREQUAL "libfuzzer")
    target_compile_options(${target_name} PRIVATE
      -fno-omit-frame-pointer
      -fsanitize=fuzzer,address,undefined
    )
    target_link_options(${target_name} PRIVATE
      -fsanitize=fuzzer,address,undefined
    )
  elseif(SUNSPOTS_FUZZ_ENGINE STREQUAL "afl")
    target_compile_definitions(${target_name} PRIVATE SUNSPOTS_FUZZ_ENGINE_AFL=1)
    target_compile_options(${target_name} PRIVATE
      -O1
      -g
      -fno-omit-frame-pointer
    )
  endif()
endfunction()

sunspots_add_fuzzer(http_request_fuzzer http_request_fuzzer.cpp)
target_link_libraries(http_request_fuzzer
  PRIVATE
    sunspots_frontend_core
)

sunspots_add_fuzzer(openmeteo_transform_fuzzer openmeteo_transform_fuzzer.cpp)
target_link_libraries(openmeteo_transform_fuzzer
  PRIVATE
    sunspots_weather_transform
)

sunspots_add_fuzzer(config_args_fuzzer config_args_fuzzer.cpp)
target_link_libraries(config_args_fuzzer
  PRIVATE
    sunspots_config
)

sunspots_add_fuzzer(compute_calculator_fuzzer compute_calculator_fuzzer.cpp)
target_link_libraries(compute_calculator_fuzzer
  PRIVATE
    sunspots_compute
)

add_library(sunspots_fetch_manager_fuzz_obj OBJECT
  ${CMAKE_SOURCE_DIR}/src/fetch/fetch_manager.c
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

sunspots_add_fuzzer(fetch_manager_config_fuzzer fetch_manager_config_fuzzer.cpp)
target_link_libraries(fetch_manager_config_fuzzer
  PRIVATE
    sunspots_curly
    sunspots_cjson
    Threads::Threads
    $<TARGET_OBJECTS:sunspots_fetch_manager_fuzz_obj>
)
