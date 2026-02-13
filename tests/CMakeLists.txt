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
