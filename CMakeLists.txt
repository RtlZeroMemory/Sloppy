cmake_minimum_required(VERSION 3.25)

project(Sloppy VERSION 0.0.0 LANGUAGES C)

include(CheckLanguage)
check_language(CXX)
if(CMAKE_CXX_COMPILER)
    enable_language(CXX)
endif()

set(SLOPPY_ENABLE_ASSERTS_DEFAULT ON)
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    set(SLOPPY_ENABLE_ASSERTS_DEFAULT OFF)
endif()

option(SLOPPY_ENABLE_TESTS "Build and register Sloppy tests." ON)
option(SLOPPY_ENABLE_ASSERTS "Enable Sloppy runtime assertions." ${SLOPPY_ENABLE_ASSERTS_DEFAULT})
option(SLOPPY_ENABLE_WERROR "Treat compiler warnings as errors." OFF)
option(SLOPPY_ENABLE_ASAN "Enable AddressSanitizer where supported." OFF)
option(SLOPPY_ENABLE_UBSAN "Enable UndefinedBehaviorSanitizer where supported." OFF)
option(SLOPPY_ENABLE_TSAN "Enable ThreadSanitizer where supported." OFF)
option(SLOPPY_BUILD_COMPILER "Add the sloppyc Rust compiler target when Cargo is available." ON)

set(SLOPPY_ENGINE "v8" CACHE STRING "JavaScript engine backend placeholder.")
set_property(CACHE SLOPPY_ENGINE PROPERTY STRINGS "v8" "none")
set(SLOPPY_V8_ROOT "" CACHE PATH "Path to a future prebuilt V8 SDK root.")

find_program(CARGO_EXECUTABLE cargo)
find_program(CLANG_FORMAT_EXECUTABLE clang-format)
find_program(CLANG_TIDY_EXECUTABLE clang-tidy)

if(NOT SLOPPY_ENGINE STREQUAL "v8" AND NOT SLOPPY_ENGINE STREQUAL "none")
    message(FATAL_ERROR "SLOPPY_ENGINE must be 'v8' or 'none'.")
endif()

if(SLOPPY_ENGINE STREQUAL "v8")
    message(STATUS "SLOPPY_ENGINE=v8 selected; V8 is not required for the foundation build.")
    if(SLOPPY_V8_ROOT)
        message(STATUS "Future V8 SDK root: ${SLOPPY_V8_ROOT}")
    endif()
endif()

set(
    SLOPPY_CORE_SOURCES
    src/core/status.c
    src/core/source_loc.c
    src/core/string.c
    src/core/bytes.c
    src/core/checked_math.c
    src/core/arena.c
    src/core/scope.c
    src/core/plan.c
    src/core/diagnostics.c)

set(
    SLOPPY_C_LINT_SOURCES
    src/main.c
    ${SLOPPY_CORE_SOURCES}
    tests/unit/core/test_status.c
    tests/unit/core/test_source_loc.c
    tests/unit/core/test_string.c
    tests/unit/core/test_bytes.c
    tests/unit/core/test_checked_math.c
    tests/unit/core/test_arena.c
    tests/unit/core/test_scope.c
    tests/unit/core/test_plan.c
    tests/unit/core/test_diagnostics.c
    tests/unit/core/test_assert.c
    tests/unit/core/test_assert_enabled_under_ndebug.c)

add_library(sloppy_core STATIC ${SLOPPY_CORE_SOURCES})
target_include_directories(sloppy_core PUBLIC "${PROJECT_SOURCE_DIR}/include")
target_compile_features(sloppy_core PUBLIC c_std_17)

if(SLOPPY_ENABLE_ASSERTS)
    target_compile_definitions(sloppy_core PUBLIC SL_ENABLE_ASSERTS=1)
else()
    target_compile_definitions(sloppy_core PUBLIC SL_ENABLE_ASSERTS=0)
endif()

add_executable(sloppy src/main.c)
target_link_libraries(sloppy PRIVATE sloppy_core)
target_include_directories(sloppy PRIVATE "${PROJECT_SOURCE_DIR}/include")
target_compile_features(sloppy PRIVATE c_std_17)

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
                    -Wstrict-prototypes
                    -Wmissing-prototypes)
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
                -Wstrict-prototypes
                -Wmissing-prototypes)
        if(SLOPPY_ENABLE_WERROR)
            target_compile_options(${target_name} PRIVATE -Werror)
        endif()
    endif()
endfunction()

function(sloppy_apply_sanitizers target_name)
    set(sanitizer_flags "")

    if(SLOPPY_ENABLE_ASAN)
        list(APPEND sanitizer_flags -fsanitize=address)
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
sloppy_apply_warnings(sloppy_core)
sloppy_apply_sanitizers(sloppy)
sloppy_apply_sanitizers(sloppy_core)

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
    list(TRANSFORM SLOPPY_C_LINT_SOURCES PREPEND "${PROJECT_SOURCE_DIR}/")
    add_custom_target(
        sloppy_clang_tidy
        COMMAND "${CLANG_TIDY_EXECUTABLE}" ${SLOPPY_C_LINT_SOURCES} -p "${CMAKE_BINARY_DIR}"
                "--warnings-as-errors=*"
        WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
        COMMENT "Running clang-tidy"
        VERBATIM)
else()
    add_custom_target(
        sloppy_clang_tidy
        COMMAND "${CMAKE_COMMAND}" -E echo "clang-tidy was not found; install it to run this gate."
        COMMAND "${CMAKE_COMMAND}" -E false
        VERBATIM)
endif()

if(SLOPPY_ENABLE_TESTS)
    include(CTest)

    function(sloppy_add_c_unit_test target_name test_name source_file)
        add_executable(${target_name} ${source_file})
        target_link_libraries(${target_name} PRIVATE sloppy_core)
        target_include_directories(${target_name} PRIVATE "${PROJECT_SOURCE_DIR}/include")
        target_compile_features(${target_name} PRIVATE c_std_17)
        sloppy_apply_warnings(${target_name})
        sloppy_apply_sanitizers(${target_name})
        add_test(NAME ${test_name} COMMAND ${target_name})
    endfunction()

    sloppy_add_c_unit_test(core_status_basic core.status.basic tests/unit/core/test_status.c)
    sloppy_add_c_unit_test(
        core_source_loc_current core.source_loc.current tests/unit/core/test_source_loc.c)
    sloppy_add_c_unit_test(core_str_views core.str.views tests/unit/core/test_string.c)
    sloppy_add_c_unit_test(core_bytes_views core.bytes.views tests/unit/core/test_bytes.c)
    sloppy_add_c_unit_test(
        core_checked_math_overflow core.checked_math.overflow tests/unit/core/test_checked_math.c)
    sloppy_add_c_unit_test(core_arena_foundation core.arena.foundation tests/unit/core/test_arena.c)
    sloppy_add_c_unit_test(core_scope_lifetime core.scope.lifetime tests/unit/core/test_scope.c)
    sloppy_add_c_unit_test(core_plan_contract core.plan.contract tests/unit/core/test_plan.c)
    set_tests_properties(core.plan.contract PROPERTIES WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}")
    sloppy_add_c_unit_test(
        core_diagnostics_foundation core.diagnostics.foundation tests/unit/core/test_diagnostics.c)
    set_tests_properties(
        core.diagnostics.foundation PROPERTIES WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}")
    sloppy_add_c_unit_test(core_assert_compiles core.assert.compiles tests/unit/core/test_assert.c)

    add_executable(
        core_assert_enabled_under_ndebug tests/unit/core/test_assert_enabled_under_ndebug.c)
    target_include_directories(
        core_assert_enabled_under_ndebug PRIVATE "${PROJECT_SOURCE_DIR}/include")
    target_compile_features(core_assert_enabled_under_ndebug PRIVATE c_std_17)
    target_compile_definitions(core_assert_enabled_under_ndebug PRIVATE SL_ENABLE_ASSERTS=1 NDEBUG)
    sloppy_apply_warnings(core_assert_enabled_under_ndebug)
    sloppy_apply_sanitizers(core_assert_enabled_under_ndebug)
    add_test(NAME core.assert.enabled_under_ndebug COMMAND core_assert_enabled_under_ndebug)
    set_tests_properties(core.assert.enabled_under_ndebug PROPERTIES WILL_FAIL TRUE)

    if(CMAKE_CXX_COMPILER)
        add_executable(core_source_loc_cpp_syntax tests/unit/core/test_source_loc_cpp.cpp)
        target_include_directories(
            core_source_loc_cpp_syntax PRIVATE "${PROJECT_SOURCE_DIR}/include")
        target_compile_features(core_source_loc_cpp_syntax PRIVATE cxx_std_17)
        add_test(NAME core.source_loc.cpp_syntax COMMAND core_source_loc_cpp_syntax)
    endif()

    add_test(NAME sloppy.cli.version COMMAND sloppy --version)
    set_tests_properties(sloppy.cli.version PROPERTIES PASS_REGULAR_EXPRESSION "Sloppy")

    add_test(NAME sloppy.cli.help COMMAND sloppy --help)
    set_tests_properties(sloppy.cli.help PROPERTIES PASS_REGULAR_EXPRESSION "Foundation build")

    if(CARGO_EXECUTABLE)
        add_test(
            NAME sloppyc.cli.version
            COMMAND "${CARGO_EXECUTABLE}" run --manifest-path
                    "${PROJECT_SOURCE_DIR}/compiler/Cargo.toml" -- --version
            WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}")
        set_tests_properties(sloppyc.cli.version PROPERTIES PASS_REGULAR_EXPRESSION "sloppyc")
    endif()
endif()

if(SLOPPY_BUILD_COMPILER)
    if(CARGO_EXECUTABLE)
        add_custom_target(
            sloppyc
            ALL
            COMMAND "${CARGO_EXECUTABLE}" build --manifest-path
                    "${PROJECT_SOURCE_DIR}/compiler/Cargo.toml"
            WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}/compiler"
            COMMENT "Building sloppyc placeholder"
            VERBATIM)
    else()
        message(WARNING "Cargo was not found; the sloppyc CMake target will not be available.")
    endif()
endif()

install(TARGETS sloppy RUNTIME DESTINATION bin)
# TODO: install sloppyc after the compiler packaging and artifact layout are defined.
