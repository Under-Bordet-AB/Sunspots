function(sunspots_add_valgrind_test test_name target_name)
  if(SUNSPOTS_ENABLE_VALGRIND AND VALGRIND_EXECUTABLE AND NOT SUNSPOTS_ENABLE_SANITIZERS)
    add_test(
      NAME valgrind.${test_name}
      COMMAND ${VALGRIND_EXECUTABLE}
        --error-exitcode=1
        --leak-check=full
        --track-origins=yes
        $<TARGET_FILE:${target_name}>
    )
    set_tests_properties(valgrind.${test_name} PROPERTIES LABELS "valgrind")
  endif()
endfunction()

add_executable(sample_test
  unit/sample_test.cpp
)
target_link_libraries(sample_test
  PRIVATE
    GTest::gtest_main
    sunspots_weather_transform
)
target_compile_features(sample_test PRIVATE cxx_std_11)
sunspots_apply_target_defaults(sample_test)
add_test(NAME sample.test COMMAND sample_test)
set_tests_properties(sample.test PROPERTIES LABELS "unit;component:sample")
sunspots_add_valgrind_test(sample.test sample_test)

add_executable(config_module_test
  unit/config_module_test.cpp
)
target_link_libraries(config_module_test
  PRIVATE
    GTest::gtest_main
    sunspots_config
)
target_compile_features(config_module_test PRIVATE cxx_std_11)
sunspots_apply_target_defaults(config_module_test)
add_test(NAME config.module.test COMMAND config_module_test)
set_tests_properties(config.module.test PROPERTIES LABELS "unit;component:config")
sunspots_add_valgrind_test(config.module.test config_module_test)

add_executable(sdk_db_module_test
  unit/sdk_db_module_test.cpp
)
target_link_libraries(sdk_db_module_test
  PRIVATE
    GTest::gtest_main
    sunspots_sdk
)
target_compile_features(sdk_db_module_test PRIVATE cxx_std_11)
sunspots_apply_target_defaults(sdk_db_module_test)
add_test(NAME sdk.db.module.test COMMAND sdk_db_module_test)
set_tests_properties(sdk.db.module.test PROPERTIES LABELS "unit;component:sdk")
sunspots_add_valgrind_test(sdk.db.module.test sdk_db_module_test)

add_executable(sdk_log_module_test
  unit/sdk_log_module_test.cpp
)
target_link_libraries(sdk_log_module_test
  PRIVATE
    GTest::gtest_main
    sunspots_sdk
)
target_compile_features(sdk_log_module_test PRIVATE cxx_std_11)
sunspots_apply_target_defaults(sdk_log_module_test)
add_test(NAME sdk.log.module.test COMMAND sdk_log_module_test)
set_tests_properties(sdk.log.module.test PROPERTIES LABELS "unit;component:sdk")
sunspots_add_valgrind_test(sdk.log.module.test sdk_log_module_test)

add_executable(atomic_file_rw_test
  unit/atomic_file_rw_test.cpp
)
target_include_directories(atomic_file_rw_test
  PRIVATE
    ${CMAKE_SOURCE_DIR}/src/libs
)
target_link_libraries(atomic_file_rw_test
  PRIVATE
    GTest::gtest_main
    sunspots_cjson
)
target_compile_features(atomic_file_rw_test PRIVATE cxx_std_11)
sunspots_apply_target_defaults(atomic_file_rw_test)
add_test(NAME libs.atomic_file_rw.test COMMAND atomic_file_rw_test)
set_tests_properties(libs.atomic_file_rw.test PROPERTIES LABELS "unit;component:libs")
sunspots_add_valgrind_test(libs.atomic_file_rw.test atomic_file_rw_test)
