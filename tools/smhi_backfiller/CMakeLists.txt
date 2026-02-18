add_executable(smhi_backfiller
    smhi_backfiller.c
)

target_include_directories(smhi_backfiller
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(smhi_backfiller
    PRIVATE
        sunspots_sdk
        sunspots_cjson
        CURL::libcurl
        Threads::Threads
)

target_compile_features(smhi_backfiller PRIVATE c_std_99)
sunspots_apply_target_defaults(smhi_backfiller)
