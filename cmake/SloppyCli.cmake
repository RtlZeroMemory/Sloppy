add_executable(sloppy src/main.c src/cli/sloppyrc.c)
target_link_libraries(sloppy PRIVATE sloppy_core)
target_link_libraries(sloppy PRIVATE yyjson::yyjson ${SLOPPY_LIBUV_TARGET})
target_include_directories(sloppy PRIVATE "${PROJECT_SOURCE_DIR}/include" "${PROJECT_SOURCE_DIR}/src")
target_compile_features(sloppy PRIVATE c_std_17)
target_compile_definitions(
    sloppy
    PRIVATE SLOPPY_BOOTSTRAP_BUILD_DIR="${SLOPPY_BOOTSTRAP_BUILD_DIR}"
            SLOPPY_COMPILER_BUILD_PATH="${PROJECT_SOURCE_DIR}/compiler/target/debug/sloppyc${CMAKE_EXECUTABLE_SUFFIX}")
if(WIN32 AND MSVC)
    # The V8-backed HTTP transport enters JavaScript from a deeper libuv callback stack than
    # --once dispatch. Reserve enough process stack for V8's guard to allow real handler entry.
    target_link_options(sloppy PRIVATE "/STACK:8388608")
endif()
