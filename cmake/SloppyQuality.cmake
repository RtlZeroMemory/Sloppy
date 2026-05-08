function(sloppy_apply_warnings target_name)
    if(MSVC)
        target_compile_options(${target_name} PRIVATE /W4)
        if(CMAKE_C_COMPILER_ID MATCHES "Clang")
            target_compile_options(
                ${target_name}
                PRIVATE
                    -Wextra
                    -Wpedantic
                    -Wconversion
                    -Wsign-conversion
                    -Wshadow
                    "$<$<COMPILE_LANGUAGE:C>:-Wstrict-prototypes>"
                    "$<$<COMPILE_LANGUAGE:C>:-Wmissing-prototypes>")
        endif()
        if(SLOPPY_ENABLE_WERROR)
            target_compile_options(${target_name} PRIVATE /WX)
        endif()
    elseif(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
        target_compile_options(
            ${target_name}
            PRIVATE
                -Wall
                -Wextra
                -Wpedantic
                -Wconversion
                -Wsign-conversion
                -Wshadow
                "$<$<COMPILE_LANGUAGE:C>:-Wstrict-prototypes>"
                "$<$<COMPILE_LANGUAGE:C>:-Wmissing-prototypes>")
        if(SLOPPY_ENABLE_WERROR)
            target_compile_options(${target_name} PRIVATE -Werror)
        endif()
    endif()
endfunction()

function(sloppy_apply_sanitizers target_name)
    set(sanitizer_flags "")

    if(SLOPPY_ENABLE_ASAN)
        if(MSVC)
            target_compile_options(${target_name} PRIVATE /fsanitize=address)
            target_link_libraries(${target_name} PRIVATE "${SLOPPY_CLANG_ASAN_DYNAMIC_LIB}")
            target_link_options(
                ${target_name} PRIVATE "/wholearchive:${SLOPPY_CLANG_ASAN_THUNK_LIB}")
            add_custom_command(
                TARGET ${target_name}
                POST_BUILD
                COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${SLOPPY_CLANG_ASAN_DLL}"
                        "$<TARGET_FILE_DIR:${target_name}>"
                COMMENT "Copying Clang ASan runtime for ${target_name}"
                VERBATIM)
        else()
            list(APPEND sanitizer_flags -fsanitize=address)
        endif()
    endif()

    if(SLOPPY_ENABLE_UBSAN)
        if(WIN32)
            message(WARNING "UBSan support on Windows is toolchain-dependent; continuing anyway.")
        endif()
        list(APPEND sanitizer_flags -fsanitize=undefined)
    endif()

    if(SLOPPY_ENABLE_TSAN)
        if(WIN32)
            message(WARNING "TSan is not generally available for the Windows foundation build.")
        endif()
        list(APPEND sanitizer_flags -fsanitize=thread)
    endif()

    if(sanitizer_flags)
        target_compile_options(${target_name} PRIVATE ${sanitizer_flags})
        target_link_options(${target_name} PRIVATE ${sanitizer_flags})
    endif()
endfunction()

sloppy_apply_warnings(sloppy)
sloppy_apply_warnings(sloppy_bench)
sloppy_apply_warnings(sloppy_core)
sloppy_apply_sanitizers(sloppy)
sloppy_apply_sanitizers(sloppy_bench)
sloppy_apply_sanitizers(sloppy_core)
sloppy_apply_sanitizers(sloppy_ryu)

file(
    GLOB_RECURSE
    SLOPPY_FORMAT_SOURCES
    CONFIGURE_DEPENDS
    "${PROJECT_SOURCE_DIR}/include/*.h"
    "${PROJECT_SOURCE_DIR}/src/*.c"
    "${PROJECT_SOURCE_DIR}/src/*.h"
    "${PROJECT_SOURCE_DIR}/src/*.cc"
    "${PROJECT_SOURCE_DIR}/src/*.cpp"
    "${PROJECT_SOURCE_DIR}/src/*.hpp"
    "${PROJECT_SOURCE_DIR}/benchmarks/*.c"
    "${PROJECT_SOURCE_DIR}/benchmarks/*.h"
    "${PROJECT_SOURCE_DIR}/tests/*.c"
    "${PROJECT_SOURCE_DIR}/tests/*.h"
    "${PROJECT_SOURCE_DIR}/tests/*.cc"
    "${PROJECT_SOURCE_DIR}/tests/*.cpp"
    "${PROJECT_SOURCE_DIR}/tests/*.hpp")

if(CLANG_FORMAT_EXECUTABLE)
    add_custom_target(
        sloppy_format_check
        COMMAND "${CLANG_FORMAT_EXECUTABLE}" --dry-run --Werror ${SLOPPY_FORMAT_SOURCES}
        WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
        COMMENT "Checking C/C++ formatting"
        VERBATIM)
else()
    add_custom_target(
        sloppy_format_check
        COMMAND "${CMAKE_COMMAND}" -E echo "clang-format was not found; install it to run this gate."
        COMMAND "${CMAKE_COMMAND}" -E false
        VERBATIM)
endif()

if(CLANG_TIDY_EXECUTABLE)
    list(TRANSFORM SLOPPY_MEMORY_ANALYSIS_SOURCES PREPEND "${PROJECT_SOURCE_DIR}/")
    list(TRANSFORM SLOPPY_C_LINT_SOURCES PREPEND "${PROJECT_SOURCE_DIR}/")
    add_custom_target(
        sloppy_memory_analysis
        COMMAND "${CLANG_TIDY_EXECUTABLE}" ${SLOPPY_MEMORY_ANALYSIS_SOURCES} -p
                "${CMAKE_BINARY_DIR}" "--warnings-as-errors=*"
        WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
        COMMENT "Running memory/core clang-tidy analysis"
        VERBATIM)
    add_custom_target(
        sloppy_clang_tidy
        COMMAND "${CLANG_TIDY_EXECUTABLE}" ${SLOPPY_C_LINT_SOURCES} -p "${CMAKE_BINARY_DIR}"
                "--warnings-as-errors=*"
        WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
        COMMENT "Running clang-tidy"
        VERBATIM)
else()
    add_custom_target(
        sloppy_memory_analysis
        COMMAND "${CMAKE_COMMAND}" -E echo "clang-tidy was not found; install it to run this gate."
        COMMAND "${CMAKE_COMMAND}" -E false
        VERBATIM)
    add_custom_target(
        sloppy_clang_tidy
        COMMAND "${CMAKE_COMMAND}" -E echo "clang-tidy was not found; install it to run this gate."
        COMMAND "${CMAKE_COMMAND}" -E false
        VERBATIM)
endif()
