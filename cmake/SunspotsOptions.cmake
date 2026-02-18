option(SUNSPOTS_ENABLE_SANITIZERS "Enable address+undefined sanitizers in Debug builds" ON)
option(SUNSPOTS_ENABLE_CLANG_TIDY "Enable clang-tidy checks for project targets" OFF)
option(SUNSPOTS_BUILD_BENCHMARKS "Build benchmark targets" ON)
option(SUNSPOTS_ENABLE_VALGRIND "Enable valgrind-backed test targets when valgrind is available" ON)
option(SUNSPOTS_BUILD_FUZZERS "Build fuzzing harness targets" OFF)
set(SUNSPOTS_FUZZ_ENGINE "libfuzzer" CACHE STRING "Fuzzing engine to use for harness targets")
set_property(CACHE SUNSPOTS_FUZZ_ENGINE PROPERTY STRINGS libfuzzer afl)

if(SUNSPOTS_ENABLE_CLANG_TIDY)
  find_program(SUNSPOTS_CLANG_TIDY_BIN NAMES clang-tidy)
  if(NOT SUNSPOTS_CLANG_TIDY_BIN)
    message(WARNING "SUNSPOTS_ENABLE_CLANG_TIDY=ON but clang-tidy was not found")
  endif()
endif()

function(sunspots_apply_target_defaults target_name)
  target_compile_definitions(${target_name} PRIVATE _POSIX_C_SOURCE=200809L)

  if(MSVC)
    target_compile_options(${target_name} PRIVATE /W3)
  else()
    target_compile_options(${target_name} PRIVATE -Wall)
  endif()

  if(SUNSPOTS_ENABLE_SANITIZERS)
    if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU" OR CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
      target_compile_options(${target_name} PRIVATE
        $<$<CONFIG:Debug>:-fno-omit-frame-pointer>
        $<$<CONFIG:Debug>:-fsanitize=address,undefined>
      )
      target_link_options(${target_name} PRIVATE
        $<$<CONFIG:Debug>:-fsanitize=address,undefined>
      )
    endif()
  endif()

  if(SUNSPOTS_ENABLE_CLANG_TIDY AND SUNSPOTS_CLANG_TIDY_BIN)
    set(_sunspots_tidy_args
      ${SUNSPOTS_CLANG_TIDY_BIN}
      -p=${CMAKE_BINARY_DIR}
      "-header-filter=^${CMAKE_SOURCE_DIR}/(src/(compute|config|core|fetch|frontend|sdk|transform|utils)|tests|benchmarks)/"
    )
    set_target_properties(${target_name} PROPERTIES
      C_CLANG_TIDY "${_sunspots_tidy_args}"
      CXX_CLANG_TIDY "${_sunspots_tidy_args}"
    )
  endif()
endfunction()
