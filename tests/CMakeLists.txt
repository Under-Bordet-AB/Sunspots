find_package(SQLite3 REQUIRED)

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

add_executable(sdk_canonical_module_test
  unit/sdk_canonical_module_test.cpp
)
target_link_libraries(sdk_canonical_module_test
  PRIVATE
    GTest::gtest_main
    sunspots_sdk
)
target_compile_features(sdk_canonical_module_test PRIVATE cxx_std_11)
sunspots_apply_target_defaults(sdk_canonical_module_test)
add_test(NAME sdk.canonical.module.test COMMAND sdk_canonical_module_test)
set_tests_properties(sdk.canonical.module.test PROPERTIES LABELS "unit;component:sdk")
sunspots_add_valgrind_test(sdk.canonical.module.test sdk_canonical_module_test)

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

add_executable(sdk_config_module_test
  unit/sdk_config_module_test.cpp
)
target_link_libraries(sdk_config_module_test
  PRIVATE
    GTest::gtest_main
    sunspots_sdk
)
target_compile_features(sdk_config_module_test PRIVATE cxx_std_11)
sunspots_apply_target_defaults(sdk_config_module_test)
add_test(NAME sdk.config.module.test COMMAND sdk_config_module_test)
set_tests_properties(sdk.config.module.test PROPERTIES LABELS "unit;component:sdk")
sunspots_add_valgrind_test(sdk.config.module.test sdk_config_module_test)

add_executable(backfill_worker_module_test
  unit/backfill_worker_module_test.cpp
)
target_link_libraries(backfill_worker_module_test
  PRIVATE
    GTest::gtest_main
    sunspots_sdk
    SQLite::SQLite3
)
target_compile_definitions(backfill_worker_module_test
  PRIVATE
    BACKFILL_BIN_PATH="$<TARGET_FILE:sunspots_backfill_openmeteo>"
)
add_dependencies(backfill_worker_module_test sunspots_backfill_openmeteo)
target_compile_features(backfill_worker_module_test PRIVATE cxx_std_11)
sunspots_apply_target_defaults(backfill_worker_module_test)
add_test(NAME backfill.worker.module.test COMMAND backfill_worker_module_test)
set_tests_properties(backfill.worker.module.test PROPERTIES LABELS "unit;component:backfill")
sunspots_add_valgrind_test(backfill.worker.module.test backfill_worker_module_test)
