cmake_minimum_required(VERSION 3.25)

project(Sloppy VERSION 0.0.0 LANGUAGES C)

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
option(SLOPPY_ENABLE_V8 "Enable the phase-gated V8 bridge SDK integration." OFF)

set(SLOPPY_ENGINE "none" CACHE STRING "JavaScript engine backend for this build.")
set_property(CACHE SLOPPY_ENGINE PROPERTY STRINGS "v8" "none")
set(SLOPPY_V8_ROOT "" CACHE PATH "Path to a prebuilt V8 SDK root.")
set(SLOPPY_V8_EXPECTED_REVISION "7221f49fdb6c89cce6be08005732ebcab3c45b38")
set(SLOPPY_V8_EXPECTED_CR_LIBCXX_REVISION "af4386908c3762433d412689038de6e6333f5921")
set(SLOPPY_BOOTSTRAP_SOURCE_DIR "${PROJECT_SOURCE_DIR}/stdlib/sloppy")
set(SLOPPY_BOOTSTRAP_BUILD_DIR "${CMAKE_BINARY_DIR}/lib/sloppy/bootstrap/sloppy")
set(
    SLOPPY_BOOTSTRAP_ASSETS
    README.md
    app.js
    bootstrap.manifest.json
    index.js
    results.js
    schema.js
    internal/intrinsics.js)

find_program(CARGO_EXECUTABLE cargo)
find_program(CLANG_FORMAT_EXECUTABLE clang-format)
find_program(CLANG_TIDY_EXECUTABLE clang-tidy)
find_program(NODE_EXECUTABLE node)

if(SLOPPY_ENABLE_TESTS OR SLOPPY_ENABLE_V8)
    include(CheckLanguage)
    check_language(CXX)
    if(CMAKE_CXX_COMPILER)
        enable_language(CXX)
    elseif(SLOPPY_ENABLE_V8)
        message(FATAL_ERROR "V8 bridge: enabled but no C++ compiler is available.")
    endif()
endif()

if(NOT SLOPPY_ENGINE STREQUAL "v8" AND NOT SLOPPY_ENGINE STREQUAL "none")
    message(FATAL_ERROR "SLOPPY_ENGINE must be 'v8' or 'none'.")
endif()

if(SLOPPY_ENGINE STREQUAL "v8" AND NOT SLOPPY_ENABLE_V8)
    set(SLOPPY_ENABLE_V8
        ON
        CACHE BOOL "Enable the phase-gated V8 bridge SDK integration." FORCE)
endif()

if(SLOPPY_ENABLE_V8 AND SLOPPY_ENGINE STREQUAL "none")
    set(SLOPPY_ENGINE
        "v8"
        CACHE STRING "JavaScript engine backend for this build." FORCE)
endif()

function(sloppy_require_v8_path relative_path description)
    set(required_path "${SLOPPY_V8_ROOT}/${relative_path}")
    if(NOT EXISTS "${required_path}")
        message(
            FATAL_ERROR
                "V8 bridge: enabled but ${description} is missing: ${required_path}\n"
                "Set SLOPPY_V8_ROOT to a prebuilt SDK root with include/, lib/, and optional bin/.")
endif()
endfunction()

function(sloppy_require_v8_manifest_string expected_value description)
    string(JSON actual_value GET "${SLOPPY_V8_MANIFEST}" ${ARGN})
    if(NOT actual_value STREQUAL expected_value)
        message(
            FATAL_ERROR
                "V8 bridge: SDK manifest ${description} is incompatible.\n"
                "Expected '${expected_value}', got '${actual_value}'.\n"
                "Rebuild/package V8 with tools/windows/build-v8.ps1.")
    endif()
endfunction()

function(sloppy_require_v8_manifest_true description)
    string(JSON actual_value GET "${SLOPPY_V8_MANIFEST}" ${ARGN})
    if(NOT actual_value)
        message(
            FATAL_ERROR
                "V8 bridge: SDK manifest ${description} must be true.\n"
                "Rebuild/package V8 with tools/windows/build-v8.ps1.")
    endif()
endfunction()

if(SLOPPY_ENABLE_V8)
    if(NOT SLOPPY_V8_ROOT)
        message(
            FATAL_ERROR
                "V8 bridge: enabled but SLOPPY_V8_ROOT is empty.\n"
                "Configure with -DSLOPPY_ENABLE_V8=ON -DSLOPPY_V8_ROOT=<sdk-root> or leave V8 disabled.")
    endif()

    if(NOT IS_DIRECTORY "${SLOPPY_V8_ROOT}")
        message(
            FATAL_ERROR
                "V8 bridge: enabled but SLOPPY_V8_ROOT is not a directory: ${SLOPPY_V8_ROOT}")
    endif()

    sloppy_require_v8_path("include/v8.h" "include/v8.h")
    sloppy_require_v8_path("include/libplatform/libplatform.h" "include/libplatform/libplatform.h")
    sloppy_require_v8_path("lib" "lib directory")
    sloppy_require_v8_path("share/sloppy-v8-sdk.json" "V8 SDK manifest")

    file(READ "${SLOPPY_V8_ROOT}/share/sloppy-v8-sdk.json" SLOPPY_V8_MANIFEST)
    sloppy_require_v8_manifest_string("sloppy-v8-sdk" "name" name)
    sloppy_require_v8_manifest_string("windows-x64" "platform" platform)
    sloppy_require_v8_manifest_string("${SLOPPY_V8_EXPECTED_REVISION}" "v8Revision" v8Revision)
    sloppy_require_v8_manifest_string("release" "buildType" buildType)
    sloppy_require_v8_manifest_string(
        "Release or RelWithDebInfo" "crtCompatibility" crtCompatibility)
    sloppy_require_v8_manifest_string(
        "${SLOPPY_V8_EXPECTED_CR_LIBCXX_REVISION}" "abi.crLibcxxRevision" abi
        crLibcxxRevision)
    sloppy_require_v8_manifest_string("x64" "abi.v8TargetArch" abi v8TargetArch)
    sloppy_require_v8_manifest_true("abi.v8CompressPointers" abi v8CompressPointers)
    sloppy_require_v8_manifest_true(
        "abi.v8CompressPointersInSharedCage" abi v8CompressPointersInSharedCage)
    sloppy_require_v8_manifest_true(
        "abi.v8_31BitSmisOn64BitArch" abi v8_31BitSmisOn64BitArch)
    sloppy_require_v8_manifest_true("abi.v8EnableSandbox" abi v8EnableSandbox)

    file(
        GLOB
        SLOPPY_V8_MONOLITH_LIBS
        CONFIGURE_DEPENDS
        "${SLOPPY_V8_ROOT}/lib/v8_monolith*.lib")
    file(
        GLOB
        SLOPPY_V8_SPLIT_CORE_LIBS
        CONFIGURE_DEPENDS
        "${SLOPPY_V8_ROOT}/lib/v8.lib")
    file(
        GLOB
        SLOPPY_V8_PLATFORM_LIBS
        CONFIGURE_DEPENDS
        "${SLOPPY_V8_ROOT}/lib/v8_libplatform*.lib")
    file(GLOB SLOPPY_V8_BASE_LIBS CONFIGURE_DEPENDS "${SLOPPY_V8_ROOT}/lib/v8_libbase*.lib")
    file(GLOB SLOPPY_V8_LIBCXX_LIBS CONFIGURE_DEPENDS "${SLOPPY_V8_ROOT}/lib/libc++*.lib")

    if(SLOPPY_V8_MONOLITH_LIBS)
        if(NOT SLOPPY_V8_PLATFORM_LIBS)
            message(
                FATAL_ERROR
                    "V8 bridge: enabled but no v8_libplatform*.lib files were found under ${SLOPPY_V8_ROOT}/lib.")
        endif()
        if(NOT SLOPPY_V8_BASE_LIBS)
            message(
                FATAL_ERROR
                    "V8 bridge: enabled but no v8_libbase*.lib files were found under ${SLOPPY_V8_ROOT}/lib.")
        endif()
        set(SLOPPY_V8_LIBS ${SLOPPY_V8_MONOLITH_LIBS} ${SLOPPY_V8_PLATFORM_LIBS}
                           ${SLOPPY_V8_BASE_LIBS} ${SLOPPY_V8_LIBCXX_LIBS})
        if(SLOPPY_V8_LIBCXX_LIBS)
            sloppy_require_v8_path("support/libcxx/include/memory" "libc++ support headers")
            sloppy_require_v8_path("support/libcxx/buildtools/__config_site"
                                   "libc++ build configuration header")
            if(CMAKE_BUILD_TYPE STREQUAL "Debug")
                message(
                    FATAL_ERROR
                        "V8 bridge: this SDK was packaged with Chromium libc++ release libraries and cannot be linked into the Debug CRT configuration.\n"
                        "Use -Preset windows-relwithdebinfo for V8 smoke tests, or build/package a matching Debug V8 SDK.")
            endif()
            message(STATUS "V8 SDK support library: libc++")
        endif()
        message(STATUS "V8 SDK library mode: monolithic")
    elseif(SLOPPY_V8_SPLIT_CORE_LIBS)
        if(NOT SLOPPY_V8_PLATFORM_LIBS)
            message(
                FATAL_ERROR
                    "V8 bridge: enabled but no v8_libplatform*.lib files were found under ${SLOPPY_V8_ROOT}/lib.")
        endif()
        if(NOT SLOPPY_V8_BASE_LIBS)
            message(
                FATAL_ERROR
                    "V8 bridge: enabled but no v8_libbase*.lib files were found under ${SLOPPY_V8_ROOT}/lib.")
        endif()
        set(SLOPPY_V8_LIBS ${SLOPPY_V8_SPLIT_CORE_LIBS} ${SLOPPY_V8_PLATFORM_LIBS}
                           ${SLOPPY_V8_BASE_LIBS})
        message(STATUS "V8 SDK library mode: split")
    else()
        message(
            FATAL_ERROR
                "V8 bridge: enabled but no core V8 library was found under ${SLOPPY_V8_ROOT}/lib.\n"
                "Expected v8_monolith*.lib, or v8.lib with v8_libplatform*.lib and v8_libbase*.lib.")
    endif()

    add_library(Sloppy::V8 INTERFACE IMPORTED)
    target_include_directories(Sloppy::V8 INTERFACE "${SLOPPY_V8_ROOT}/include")
    target_compile_definitions(
        Sloppy::V8
        INTERFACE
            $<$<COMPILE_LANGUAGE:CXX>:V8_COMPRESS_POINTERS>
            $<$<COMPILE_LANGUAGE:CXX>:V8_COMPRESS_POINTERS_IN_SHARED_CAGE>
            $<$<COMPILE_LANGUAGE:CXX>:V8_31BIT_SMIS_ON_64BIT_ARCH>
            $<$<COMPILE_LANGUAGE:CXX>:V8_ENABLE_SANDBOX>
            $<$<COMPILE_LANGUAGE:CXX>:V8_TARGET_ARCH_X64>)
    if(SLOPPY_V8_LIBCXX_LIBS)
        target_compile_definitions(
            Sloppy::V8
            INTERFACE
                $<$<COMPILE_LANGUAGE:CXX>:_LIBCPP_HARDENING_MODE=_LIBCPP_HARDENING_MODE_EXTENSIVE>
                $<$<COMPILE_LANGUAGE:CXX>:_LIBCPP_DISABLE_VISIBILITY_ANNOTATIONS>
                $<$<COMPILE_LANGUAGE:CXX>:CR_LIBCXX_REVISION=${SLOPPY_V8_EXPECTED_CR_LIBCXX_REVISION}>)
        target_compile_options(
            Sloppy::V8
            INTERFACE
                $<$<COMPILE_LANGUAGE:CXX>:/I${SLOPPY_V8_ROOT}/support/libcxx/buildtools>
                $<$<COMPILE_LANGUAGE:CXX>:/I${SLOPPY_V8_ROOT}/support/libcxx/include>)
    endif()
    target_link_libraries(Sloppy::V8 INTERFACE ${SLOPPY_V8_LIBS})
    if(WIN32)
        target_link_libraries(
            Sloppy::V8
            INTERFACE winmm
                      dbghelp
                      $<$<CONFIG:Debug>:vcruntimed>
                      $<$<NOT:$<CONFIG:Debug>>:vcruntime>
                      $<$<CONFIG:Debug>:msvcprtd>
                      $<$<NOT:$<CONFIG:Debug>>:msvcprt>)
    endif()

    message(STATUS "V8 bridge: enabled")
    message(STATUS "V8 SDK root: ${SLOPPY_V8_ROOT}")
else()
    message(STATUS "V8 bridge: disabled")
endif()

find_package(yyjson CONFIG REQUIRED)
find_package(llhttp CONFIG REQUIRED)
find_package(libuv CONFIG REQUIRED)

if(TARGET llhttp::llhttp_static)
    set(SLOPPY_LLHTTP_TARGET llhttp::llhttp_static)
elseif(TARGET llhttp::llhttp)
    set(SLOPPY_LLHTTP_TARGET llhttp::llhttp)
elseif(TARGET llhttp)
    set(SLOPPY_LLHTTP_TARGET llhttp)
else()
    message(FATAL_ERROR "llhttp dependency was found, but no known CMake target is available.")
endif()

if(TARGET libuv::uv_a)
    set(SLOPPY_LIBUV_TARGET libuv::uv_a)
elseif(TARGET libuv::uv)
    set(SLOPPY_LIBUV_TARGET libuv::uv)
else()
    message(FATAL_ERROR "libuv dependency was found, but neither libuv::uv_a nor libuv::uv is available.")
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
    src/core/loop.c
    src/core/async.c
    src/core/worker_pool.c
    src/core/http.c
    src/core/http_dispatch.c
    src/core/route.c
    src/core/plan.c
    src/core/plan_parse.c
    src/core/runtime_contract.c
    src/core/diagnostics.c
    src/engine/engine.c)

if(SLOPPY_ENABLE_V8)
    list(APPEND SLOPPY_CORE_SOURCES src/engine/v8/engine_v8.cc)
endif()

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
    tests/unit/core/test_loop.c
    tests/unit/core/test_async.c
    tests/unit/core/test_worker_pool.c
    tests/unit/core/test_http.c
    tests/unit/core/test_http_dispatch.c
    tests/unit/core/test_route.c
    tests/unit/core/test_plan.c
    tests/unit/core/test_plan_parse.c
    tests/unit/core/test_runtime_contract.c
    tests/unit/core/test_engine.c
    tests/unit/core/test_diagnostics.c
    tests/unit/core/test_assert.c
    tests/unit/core/test_assert_enabled_under_ndebug.c)

if(SLOPPY_ENABLE_V8)
    list(
        APPEND
        SLOPPY_C_LINT_SOURCES
        tests/unit/engine/test_v8_smoke.c
        tests/integration/execution/test_handwritten_artifact_execution.c
        tests/integration/http_dispatch/test_http_dispatch_execution.c)
endif()

add_library(sloppy_core STATIC ${SLOPPY_CORE_SOURCES})
target_include_directories(sloppy_core PUBLIC "${PROJECT_SOURCE_DIR}/include")
target_compile_features(sloppy_core PUBLIC c_std_17)
target_link_libraries(
    sloppy_core
    PRIVATE yyjson::yyjson
            ${SLOPPY_LLHTTP_TARGET}
            ${SLOPPY_LIBUV_TARGET})

if(SLOPPY_ENABLE_V8)
    target_compile_features(sloppy_core PRIVATE cxx_std_20)
    target_compile_definitions(sloppy_core PUBLIC SLOPPY_ENABLE_V8_BRIDGE=1)
    target_link_libraries(sloppy_core PRIVATE Sloppy::V8)
endif()

if(SLOPPY_ENABLE_ASSERTS)
    target_compile_definitions(sloppy_core PUBLIC SL_ENABLE_ASSERTS=1)
else()
    target_compile_definitions(sloppy_core PUBLIC SL_ENABLE_ASSERTS=0)
endif()

add_executable(sloppy src/main.c)
target_link_libraries(sloppy PRIVATE sloppy_core)
target_include_directories(sloppy PRIVATE "${PROJECT_SOURCE_DIR}/include")
target_compile_features(sloppy PRIVATE c_std_17)

set(SLOPPY_BOOTSTRAP_OUTPUTS "")
foreach(bootstrap_asset IN LISTS SLOPPY_BOOTSTRAP_ASSETS)
    set(bootstrap_source "${SLOPPY_BOOTSTRAP_SOURCE_DIR}/${bootstrap_asset}")
    set(bootstrap_output "${SLOPPY_BOOTSTRAP_BUILD_DIR}/${bootstrap_asset}")
    get_filename_component(bootstrap_output_dir "${bootstrap_output}" DIRECTORY)
    add_custom_command(
        OUTPUT "${bootstrap_output}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${bootstrap_output_dir}"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${bootstrap_source}" "${bootstrap_output}"
        DEPENDS "${bootstrap_source}"
        COMMENT "Copying bootstrap stdlib asset ${bootstrap_asset}"
        VERBATIM)
    list(APPEND SLOPPY_BOOTSTRAP_OUTPUTS "${bootstrap_output}")
endforeach()

add_custom_target(sloppy_bootstrap_assets ALL DEPENDS ${SLOPPY_BOOTSTRAP_OUTPUTS})

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
    sloppy_add_c_unit_test(
        core_loop_completion_queue core.loop.completion_queue tests/unit/core/test_loop.c)
    sloppy_add_c_unit_test(
        core_async_settlement core.async.settlement tests/unit/core/test_async.c)
    sloppy_add_c_unit_test(
        core_worker_pool_inline core.worker_pool.inline tests/unit/core/test_worker_pool.c)
    sloppy_add_c_unit_test(core_http_parser core.http.parser tests/unit/core/test_http.c)
    sloppy_add_c_unit_test(
        core_http_dispatch core.http.dispatch tests/unit/core/test_http_dispatch.c)
    sloppy_add_c_unit_test(core_route_pattern core.route.pattern tests/unit/core/test_route.c)
    sloppy_add_c_unit_test(core_plan_contract core.plan.contract tests/unit/core/test_plan.c)
    set_tests_properties(core.plan.contract PROPERTIES WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}")
    sloppy_add_c_unit_test(
        core_plan_parse_json core.plan.parse_json tests/unit/core/test_plan_parse.c)
    set_tests_properties(
        core.plan.parse_json PROPERTIES WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}")
    sloppy_add_c_unit_test(
        core_runtime_contract core.runtime_contract tests/unit/core/test_runtime_contract.c)
    sloppy_add_c_unit_test(core_engine_abi core.engine.abi tests/unit/core/test_engine.c)
    if(SLOPPY_ENABLE_V8)
        sloppy_add_c_unit_test(
            engine_v8_smoke engine.v8.smoke tests/unit/engine/test_v8_smoke.c)
        sloppy_add_c_unit_test(
            execution_handwritten_artifact execution.handwritten_artifact
            tests/integration/execution/test_handwritten_artifact_execution.c)
        set_tests_properties(
            execution.handwritten_artifact PROPERTIES WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}")
        sloppy_add_c_unit_test(
            http_dispatch_execution http.dispatch.execution
            tests/integration/http_dispatch/test_http_dispatch_execution.c)
        set_tests_properties(
            http.dispatch.execution PROPERTIES WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}")
    endif()
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

    add_test(
        NAME bootstrap.stdlib.assets
        COMMAND
            "${CMAKE_COMMAND}" "-DSLOPPY_BOOTSTRAP_SOURCE_DIR=${SLOPPY_BOOTSTRAP_SOURCE_DIR}"
            "-DSLOPPY_BOOTSTRAP_BUILD_DIR=${SLOPPY_BOOTSTRAP_BUILD_DIR}" -P
            "${PROJECT_SOURCE_DIR}/tests/cmake/check_bootstrap_assets.cmake")
    add_test(
        NAME bootstrap.stdlib.api_shape
        COMMAND
            "${CMAKE_COMMAND}" "-DSLOPPY_BOOTSTRAP_SOURCE_DIR=${SLOPPY_BOOTSTRAP_SOURCE_DIR}"
            "-DSLOPPY_BOOTSTRAP_BUILD_DIR=${SLOPPY_BOOTSTRAP_BUILD_DIR}" -P
            "${PROJECT_SOURCE_DIR}/tests/cmake/check_bootstrap_api.cmake")
    add_test(
        NAME examples.hello.api_shape
        COMMAND "${CMAKE_COMMAND}" "-DPROJECT_SOURCE_DIR=${PROJECT_SOURCE_DIR}" -P
                "${PROJECT_SOURCE_DIR}/tests/cmake/check_hello_example.cmake")
    add_test(
        NAME examples.ergonomics.api_shape
        COMMAND "${CMAKE_COMMAND}" "-DPROJECT_SOURCE_DIR=${PROJECT_SOURCE_DIR}" -P
                "${PROJECT_SOURCE_DIR}/tests/cmake/check_ergonomics_example.cmake")
    add_test(
        NAME examples.modules_basic.api_shape
        COMMAND "${CMAKE_COMMAND}" "-DPROJECT_SOURCE_DIR=${PROJECT_SOURCE_DIR}" -P
                "${PROJECT_SOURCE_DIR}/tests/cmake/check_modules_basic_example.cmake")

    if(NODE_EXECUTABLE)
        add_test(
            NAME bootstrap.stdlib.app_host_foundation
            COMMAND "${NODE_EXECUTABLE}"
                    "${PROJECT_SOURCE_DIR}/tests/bootstrap/test_app_host_foundation.mjs")
        add_test(
            NAME bootstrap.stdlib.modules
            COMMAND "${NODE_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tests/bootstrap/test_modules.mjs")
    endif()

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
install(DIRECTORY "${SLOPPY_BOOTSTRAP_SOURCE_DIR}" DESTINATION "lib/sloppy/bootstrap")
# TODO: install sloppyc after the compiler packaging and artifact layout are defined.
