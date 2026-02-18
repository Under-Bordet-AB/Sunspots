add_executable(sdk_db_test
    sdk_db_test.c
)

target_include_directories(sdk_db_test
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(sdk_db_test
    PRIVATE
        sunspots_sdk
        sunspots_cjson
        CURL::libcurl
        Threads::Threads
)

target_compile_features(sdk_db_test PRIVATE c_std_99)
sunspots_apply_target_defaults(sdk_db_test)
