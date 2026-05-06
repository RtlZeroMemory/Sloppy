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
if(WIN32)
    set(SLOPPY_ENABLE_SQLSERVER_DEFAULT ON)
else()
    set(SLOPPY_ENABLE_SQLSERVER_DEFAULT OFF)
endif()
option(
    SLOPPY_ENABLE_SQLSERVER
    "Enable the phase-gated SQL Server provider ODBC integration."
    ${SLOPPY_ENABLE_SQLSERVER_DEFAULT})

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
    codec.js
    data.js
    fs.js
    crypto.js
    net.js
    time.js
    index.js
    providers/sqlite.js
    results.js
    schema.js
    internal/intrinsics.js
    internal/runtime-classic.js)

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
find_package(OpenSSL REQUIRED)
find_package(unofficial-sodium CONFIG REQUIRED)
find_package(xxHash CONFIG REQUIRED)
find_package(unofficial-sqlite3 CONFIG REQUIRED)
find_package(PostgreSQL REQUIRED)
find_package(ZLIB REQUIRED)
if(SLOPPY_ENABLE_SQLSERVER)
    find_package(ODBC REQUIRED)
    message(STATUS "SQL Server provider: enabled through ODBC")
else()
    message(STATUS "SQL Server provider: disabled; ODBC-backed calls report unavailable")
endif()

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

if(TARGET xxHash::xxhash)
    set(SLOPPY_XXHASH_TARGET xxHash::xxhash)
elseif(TARGET xxhash)
    set(SLOPPY_XXHASH_TARGET xxhash)
else()
    message(FATAL_ERROR "xxHash dependency was found, but no known CMake target is available.")
endif()

set(
    SLOPPY_CORE_SOURCES
    src/core/status.c
    src/core/source_loc.c
    src/core/string.c
    src/core/bytes.c
    src/core/cancellation.c
    src/core/checked_math.c
    src/core/arena.c
    src/core/builder.c
    src/core/intern.c
    src/core/scope.c
    src/core/resource.c
    src/core/capability.c
    src/core/execution_domain.c
    src/core/features.c
    src/core/crypto.c
    src/core/crypto_noncrypto_hash.c
    src/core/fs.c
    src/platform/crypto_password_sodium.c
    src/platform/libuv/net_tcp_libuv.c
    src/core/app_host.c
    src/core/loop.c
    src/core/async.c
    src/core/async_backend.c
    src/core/provider_executor.c
    src/core/worker_pool.c
    src/core/http.c
    src/core/http_backend.c
    src/core/http_context.c
    src/core/http_dispatch.c
    src/core/http_response.c
    src/core/route.c
    src/core/plan.c
    src/core/plan_parse.c
    src/core/runtime_contract.c
    src/core/diagnostics.c
    src/data/postgres.c
    src/data/sqlserver.c
    src/data/sqlite.c
    src/engine/engine.c
    src/platform/libuv/async_backend_libuv.c
    src/platform/libuv/http_transport_libuv.c
    src/platform/libuv/process.c
    src/platform/libuv/thread.c)

if(WIN32)
    list(
        APPEND
        SLOPPY_CORE_SOURCES
        src/platform/win32/time.c
        src/platform/win32/fs_win32.c
        src/platform/win32/crypto_win32.c)
else()
    list(
        APPEND
        SLOPPY_CORE_SOURCES
        src/platform/posix/time.c
        src/platform/posix/fs_posix.c
        src/platform/posix/crypto_posix.c)
endif()

if(SLOPPY_ENABLE_V8)
    list(
        APPEND
        SLOPPY_CORE_SOURCES
        src/engine/v8/engine_v8.cc
        src/engine/v8/async_scheduler.cc
        src/engine/v8/http_bridge.cc
        src/engine/v8/string_interop.cc
        src/engine/v8/intrinsics.cc
        src/engine/v8/intrinsics_crypto.cc
        src/engine/v8/intrinsics_codec.cc
        src/engine/v8/intrinsics_fs.cc
        src/engine/v8/intrinsics_net.cc
        src/engine/v8/intrinsics_time.cc
        src/engine/v8/intrinsics_sqlite.cc)
endif()

set(
    SLOPPY_C_LINT_SOURCES
    src/main.c
    ${SLOPPY_CORE_SOURCES}
    tests/unit/core/test_status.c
    tests/unit/core/test_source_loc.c
    tests/unit/core/test_string.c
    tests/unit/core/test_bytes.c
    tests/unit/core/test_cancellation.c
    tests/unit/core/test_checked_math.c
    tests/unit/core/test_arena.c
    tests/unit/core/test_builder.c
    tests/unit/core/test_intern.c
    tests/unit/core/test_scope.c
    tests/unit/core/test_resource.c
    tests/unit/core/test_capability.c
    tests/unit/core/test_execution_domain.c
    tests/unit/core/test_features.c
    tests/unit/core/test_crypto.c
    tests/unit/core/test_app_host.c
    tests/unit/core/test_loop.c
    tests/unit/core/test_async.c
    tests/unit/core/test_async_backend.c
    tests/unit/core/test_provider_executor.c
    tests/unit/core/test_worker_pool.c
    tests/unit/core/test_http.c
    tests/unit/core/test_http_backend.c
    tests/unit/core/test_http_transport.cc
    tests/unit/core/test_http_context.c
    tests/unit/core/test_http_dispatch.c
    tests/unit/core/test_http_response.c
    tests/unit/core/test_route.c
    tests/unit/core/test_plan.c
    tests/unit/core/test_plan_parse.c
    tests/unit/core/test_runtime_contract.c
    tests/unit/core/test_engine.c
    tests/unit/core/test_diagnostics.c
    tests/unit/data/test_postgres.c
    tests/unit/data/test_sqlserver.c
    tests/unit/core/test_assert.c
    tests/unit/core/test_assert_enabled_under_ndebug.c
    tests/unit/data/test_sqlite.c
    benchmarks/bench_main.c
    benchmarks/bench_route_matcher.c
    benchmarks/bench_handler_dispatch.c)

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
            ${SLOPPY_LIBUV_TARGET}
            ${SLOPPY_XXHASH_TARGET}
            OpenSSL::Crypto
            unofficial-sodium::sodium
            PostgreSQL::PostgreSQL
            unofficial::sqlite3::sqlite3
            ZLIB::ZLIB)

if(WIN32)
    target_link_libraries(sloppy_core PRIVATE bcrypt)
endif()

if(APPLE)
    target_link_libraries(sloppy_core PRIVATE "-framework Security")
endif()

if(SLOPPY_ENABLE_SQLSERVER)
    target_compile_definitions(sloppy_core PUBLIC SLOPPY_ENABLE_SQLSERVER_PROVIDER=1)
    target_link_libraries(sloppy_core PRIVATE ODBC::ODBC)
endif()

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
target_link_libraries(sloppy PRIVATE yyjson::yyjson ${SLOPPY_LIBUV_TARGET})
target_include_directories(sloppy PRIVATE "${PROJECT_SOURCE_DIR}/include")
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

add_executable(
    sloppy_bench
    benchmarks/bench_main.c
    benchmarks/bench_route_matcher.c
    benchmarks/bench_handler_dispatch.c)
target_link_libraries(sloppy_bench PRIVATE sloppy_core)
target_include_directories(
    sloppy_bench PRIVATE "${PROJECT_SOURCE_DIR}/include" "${PROJECT_SOURCE_DIR}/benchmarks")
target_compile_features(sloppy_bench PRIVATE c_std_17)

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
sloppy_apply_warnings(sloppy_bench)
sloppy_apply_warnings(sloppy_core)
sloppy_apply_sanitizers(sloppy)
sloppy_apply_sanitizers(sloppy_bench)
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

    function(sloppy_add_cxx_unit_test target_name test_name source_file)
        add_executable(${target_name} ${source_file})
        target_link_libraries(${target_name} PRIVATE sloppy_core)
        target_include_directories(${target_name} PRIVATE "${PROJECT_SOURCE_DIR}/include")
        target_compile_features(${target_name} PRIVATE cxx_std_17)
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
        core_cancellation_token core.cancellation.token tests/unit/core/test_cancellation.c)
    sloppy_add_c_unit_test(
        core_checked_math_overflow core.checked_math.overflow tests/unit/core/test_checked_math.c)
    sloppy_add_c_unit_test(core_arena_foundation core.arena.foundation tests/unit/core/test_arena.c)
    sloppy_add_c_unit_test(core_builder_foundation core.builder.foundation
                           tests/unit/core/test_builder.c)
    sloppy_add_c_unit_test(core_intern_table core.intern.table tests/unit/core/test_intern.c)
    sloppy_add_c_unit_test(core_scope_lifetime core.scope.lifetime tests/unit/core/test_scope.c)
    sloppy_add_c_unit_test(
        core_resource_lifecycle core.resource.lifecycle tests/unit/core/test_resource.c)
    sloppy_add_c_unit_test(
        core_capability_registry core.capability.registry tests/unit/core/test_capability.c)
    sloppy_add_c_unit_test(
        core_execution_domain core.execution_domain tests/unit/core/test_execution_domain.c)
    sloppy_add_c_unit_test(core_runtime_features core.runtime_features
                           tests/unit/core/test_features.c)
    set_tests_properties(core.runtime_features PROPERTIES WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}")
    sloppy_add_c_unit_test(core_crypto_foundation core.crypto.foundation
                           tests/unit/core/test_crypto.c)
    if(CMAKE_CXX_COMPILER)
        sloppy_add_cxx_unit_test(
            core_net_tcp_client core.net.tcp_client tests/unit/core/test_net_tcp_client.cc)
        target_link_libraries(core_net_tcp_client PRIVATE ${SLOPPY_LIBUV_TARGET})
        add_test(NAME conformance.net.tcp_client_loopback COMMAND $<TARGET_FILE:core_net_tcp_client>)
        set_tests_properties(
            conformance.net.tcp_client_loopback PROPERTIES LABELS "conformance;net;tcp;smoke")
    endif()
    sloppy_add_c_unit_test(core_filesystem core.filesystem tests/unit/core/test_fs.c)
    sloppy_add_c_unit_test(core_app_host_hardening core.app_host.hardening
                           tests/unit/core/test_app_host.c)
    sloppy_add_c_unit_test(
        core_loop_completion_queue core.loop.completion_queue tests/unit/core/test_loop.c)
    sloppy_add_c_unit_test(
        core_async_settlement core.async.settlement tests/unit/core/test_async.c)
    sloppy_add_c_unit_test(
        core_async_backend core.async.backend tests/unit/core/test_async_backend.c)
    add_test(NAME conformance.async.native_settlement COMMAND $<TARGET_FILE:core_async_settlement>)
    set_tests_properties(
        conformance.async.native_settlement PROPERTIES LABELS "conformance;async")
    add_test(NAME conformance.async.backend_completion COMMAND $<TARGET_FILE:core_async_backend>)
    set_tests_properties(
        conformance.async.backend_completion PROPERTIES LABELS "conformance;async")
    sloppy_add_c_unit_test(
        core_provider_executor core.provider_executor tests/unit/core/test_provider_executor.c)
    add_test(
        NAME conformance.capability.provider_executor
        COMMAND $<TARGET_FILE:core_provider_executor>)
    set_tests_properties(
        conformance.capability.provider_executor
        PROPERTIES LABELS "conformance;capability;smoke")
    if(CMAKE_CXX_COMPILER)
        sloppy_add_cxx_unit_test(
            core_async_backend_libuv core.async.backend_libuv
            tests/unit/core/test_async_backend_libuv.cc)
        add_test(
            NAME conformance.async.backend_libuv
            COMMAND $<TARGET_FILE:core_async_backend_libuv>)
        set_tests_properties(
            conformance.async.backend_libuv PROPERTIES LABELS "conformance;async;smoke")
    endif()
    sloppy_add_c_unit_test(
        core_worker_pool_inline core.worker_pool.inline tests/unit/core/test_worker_pool.c)
    sloppy_add_c_unit_test(core_http_parser core.http.parser tests/unit/core/test_http.c)
    sloppy_add_c_unit_test(
        core_http_backend core.http.backend tests/unit/core/test_http_backend.c)
    if(CMAKE_CXX_COMPILER)
        sloppy_add_cxx_unit_test(
            core_http_transport core.http.transport tests/unit/core/test_http_transport.cc)
        target_link_libraries(core_http_transport PRIVATE ${SLOPPY_LIBUV_TARGET})
        add_test(
            NAME conformance.transport.localhost_mvp
            COMMAND $<TARGET_FILE:core_http_transport> --without-bounded-smoke)
        set_tests_properties(
            conformance.transport.localhost_mvp PROPERTIES LABELS "conformance;transport;http")
        foreach(
            test_name
            IN
            ITEMS conformance.transport.keep_alive
                  conformance.transport.keep_alive_idle_timeout
                  conformance.transport.keep_alive_max_requests
                  conformance.transport.lifecycle_reset
                  conformance.transport.chunked_request
                  conformance.transport.streaming_response
                  conformance.transport.backpressure
                  conformance.transport.shutdown_cancel)
            add_test(NAME ${test_name} COMMAND $<TARGET_FILE:core_http_transport> --without-bounded-smoke)
            set_tests_properties(${test_name} PROPERTIES LABELS "conformance;transport;http")
        endforeach()
        add_test(
            NAME smoke.transport.keep_alive_streaming_bounded
            COMMAND $<TARGET_FILE:core_http_transport> --bounded-smoke-only)
        set_tests_properties(
            smoke.transport.keep_alive_streaming_bounded PROPERTIES LABELS "smoke;transport;http")
    endif()
    sloppy_add_c_unit_test(
        core_http_context core.http.context tests/unit/core/test_http_context.c)
    sloppy_add_c_unit_test(
        core_http_dispatch core.http.dispatch tests/unit/core/test_http_dispatch.c)
    add_test(NAME conformance.http.default_dispatch COMMAND $<TARGET_FILE:core_http_dispatch>)
    set_tests_properties(
        conformance.http.default_dispatch PROPERTIES LABELS "conformance;http")
    sloppy_add_c_unit_test(
        core_http_response core.http.response tests/unit/core/test_http_response.c)
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
        add_test(NAME conformance.v8.runtime_bridge COMMAND $<TARGET_FILE:engine_v8_smoke>)
        set_tests_properties(
            conformance.v8.runtime_bridge PROPERTIES LABELS "conformance;v8;http;async")
        sloppy_add_cxx_unit_test(
            engine_v8_owner_thread engine.v8.owner_thread
            tests/unit/engine/test_v8_owner_thread.cc)
        target_link_libraries(engine_v8_owner_thread PRIVATE Sloppy::V8)
        target_compile_features(engine_v8_owner_thread PRIVATE cxx_std_20)
        add_test(NAME conformance.v8.owner_thread COMMAND $<TARGET_FILE:engine_v8_owner_thread>)
        set_tests_properties(
            conformance.v8.owner_thread PROPERTIES LABELS "conformance;v8;async")
        sloppy_add_cxx_unit_test(
            engine_v8_async_scheduler engine.v8.async_scheduler
            tests/unit/engine/test_v8_async_scheduler.cc)
        target_link_libraries(engine_v8_async_scheduler PRIVATE Sloppy::V8)
        target_compile_features(engine_v8_async_scheduler PRIVATE cxx_std_20)
        add_test(
            NAME conformance.v8.native_async_scheduler
            COMMAND $<TARGET_FILE:engine_v8_async_scheduler>)
        set_tests_properties(
            conformance.v8.native_async_scheduler PROPERTIES LABELS "conformance;v8;async")
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
        add_test(
            NAME conformance.v8.http_dispatch_execution
            COMMAND $<TARGET_FILE:http_dispatch_execution>)
        set_tests_properties(
            conformance.v8.http_dispatch_execution
            PROPERTIES WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}" LABELS "conformance;v8;http")
    endif()
    sloppy_add_c_unit_test(
        core_diagnostics_foundation core.diagnostics.foundation tests/unit/core/test_diagnostics.c)
    set_tests_properties(
        core.diagnostics.foundation PROPERTIES WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}")
    sloppy_add_c_unit_test(
        data_postgres_provider data.postgres.provider tests/unit/data/test_postgres.c)
    add_test(NAME data.postgres.live_provider COMMAND data_postgres_provider --live)
    set_tests_properties(
        data.postgres.live_provider PROPERTIES SKIP_RETURN_CODE 77 LABELS "live-provider;postgres")
    sloppy_add_c_unit_test(
        data_sqlserver_provider data.sqlserver.provider tests/unit/data/test_sqlserver.c)
    add_test(NAME data.sqlserver.live_provider COMMAND data_sqlserver_provider --live)
    set_tests_properties(
        data.sqlserver.live_provider PROPERTIES SKIP_RETURN_CODE 77 LABELS "live-provider;sqlserver")
    sloppy_add_c_unit_test(data_sqlite_provider data.sqlite.provider tests/unit/data/test_sqlite.c)
    add_test(NAME conformance.sqlite.native_provider COMMAND $<TARGET_FILE:data_sqlite_provider>)
    set_tests_properties(
        conformance.sqlite.native_provider PROPERTIES LABELS "conformance;sqlite")
    add_test(
        NAME conformance.capability.native_registry
        COMMAND $<TARGET_FILE:core_capability_registry>)
    set_tests_properties(
        conformance.capability.native_registry PROPERTIES LABELS "conformance;capability")
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

    add_test(NAME benchmarks.sloppy_bench.list COMMAND sloppy_bench --list)
    set_tests_properties(
        benchmarks.sloppy_bench.list PROPERTIES PASS_REGULAR_EXPRESSION "route.match.static")

    add_test(NAME benchmarks.sloppy_bench.smoke_json COMMAND sloppy_bench --smoke --format json)
    set_tests_properties(
        benchmarks.sloppy_bench.smoke_json
        PROPERTIES PASS_REGULAR_EXPRESSION "\"sloppyBenchmarkVersion\": 1")

    add_test(
        NAME benchmarks.sloppy_bench.v8_deferred
        COMMAND sloppy_bench --include-v8 --bench handler.v8.call_function0)
    set_tests_properties(
        benchmarks.sloppy_bench.v8_deferred
        PROPERTIES PASS_REGULAR_EXPRESSION "benchmark skipped/deferred: handler.v8.call_function0")

    if(WIN32)
        add_test(
            NAME benchmarks.windows_wrapper.smoke_json
            COMMAND
                powershell -NoProfile -ExecutionPolicy Bypass -File
                "${PROJECT_SOURCE_DIR}/tests/scripts/test_bench_wrapper_json.ps1" -RepoRoot
                "${PROJECT_SOURCE_DIR}")
    endif()

    function(sloppy_add_cli_golden_test test_name expected_file)
        add_test(
            NAME ${test_name}
            COMMAND
                "${CMAKE_COMMAND}" "-DSLOPPY_CLI=$<TARGET_FILE:sloppy>"
                "-DSLOPPY_EXPECTED=${PROJECT_SOURCE_DIR}/${expected_file}"
                "-DSLOPPY_CLI_ARGS=${ARGN}" -P
                "${PROJECT_SOURCE_DIR}/tests/cmake/check_cli_output.cmake")
        set_tests_properties(${test_name} PROPERTIES WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}")
    endfunction()

    function(sloppy_add_cli_nonzero_golden_test test_name expected_file)
        add_test(
            NAME ${test_name}
            COMMAND
                "${CMAKE_COMMAND}" "-DSLOPPY_CLI=$<TARGET_FILE:sloppy>"
                "-DSLOPPY_EXPECTED=${PROJECT_SOURCE_DIR}/${expected_file}"
                "-DSLOPPY_EXPECT_NONZERO=1" "-DSLOPPY_CLI_ARGS=${ARGN}" -P
                "${PROJECT_SOURCE_DIR}/tests/cmake/check_cli_output.cmake")
        set_tests_properties(${test_name} PROPERTIES WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}")
    endfunction()

    sloppy_add_cli_golden_test(
        sloppy.cli.routes_text tests/golden/cli/routes-text.txt routes --plan
        tests/fixtures/cli/route-metadata.plan.json --format text)
    sloppy_add_cli_golden_test(
        sloppy.cli.routes_json tests/golden/cli/routes-json.json routes --plan
        tests/fixtures/cli/route-metadata.plan.json --format json)
    sloppy_add_cli_golden_test(
        sloppy.cli.routes_compiler_text tests/golden/cli/routes-compiler-text.txt routes --plan
        compiler/tests/fixtures/grouped-route/expected/app.plan.json --format text)
    sloppy_add_cli_golden_test(
        sloppy.cli.routes_compiler_json tests/golden/cli/routes-compiler-json.json routes --plan
        compiler/tests/fixtures/grouped-route/expected/app.plan.json --format json)
    sloppy_add_cli_golden_test(
        sloppy.cli.capabilities_users_text tests/golden/cli/capabilities-users-text.txt
        capabilities --plan compiler/tests/fixtures/realistic-users-api/expected/app.plan.json
        --format text)
    sloppy_add_cli_golden_test(
        sloppy.cli.capabilities_users_json tests/golden/cli/capabilities-users-json.json
        capabilities --plan compiler/tests/fixtures/realistic-users-api/expected/app.plan.json
        --format json)
    set(SLOPPY_DOCTOR_TEXT_GOLDEN tests/golden/cli/doctor-text.txt)
    set(SLOPPY_DOCTOR_JSON_GOLDEN tests/golden/cli/doctor-json.json)
    set(SLOPPY_DOCTOR_PARTIAL_TEXT_GOLDEN tests/golden/cli/doctor-partial-text.txt)
    set(SLOPPY_DOCTOR_PARTIAL_JSON_GOLDEN tests/golden/cli/doctor-partial-json.json)
    set(SLOPPY_DOCTOR_FS_TEXT_GOLDEN tests/golden/cli/doctor-filesystem-text.txt)
    set(SLOPPY_DOCTOR_FS_JSON_GOLDEN tests/golden/cli/doctor-filesystem-json.json)
    set(SLOPPY_DOCTOR_NET_TEXT_GOLDEN tests/golden/cli/doctor-network-text.txt)
    set(SLOPPY_DOCTOR_NET_JSON_GOLDEN tests/golden/cli/doctor-network-json.json)
    set(SLOPPY_DOCTOR_CRYPTO_NONCRYPTO_TEXT_GOLDEN
        tests/golden/cli/doctor-crypto-noncrypto-hash-text.txt)
    set(SLOPPY_DOCTOR_CRYPTO_NONCRYPTO_JSON_GOLDEN
        tests/golden/cli/doctor-crypto-noncrypto-hash-json.json)
    set(SLOPPY_DOCTOR_CODEC_CHECKSUM_TEXT_GOLDEN
        tests/golden/cli/doctor-codec-checksum-text.txt)
    set(SLOPPY_DOCTOR_CODEC_CHECKSUM_JSON_GOLDEN
        tests/golden/cli/doctor-codec-checksum-json.json)
    if(SLOPPY_ENABLE_V8)
        set(SLOPPY_DOCTOR_TEXT_GOLDEN tests/golden/cli/doctor-text-v8.txt)
        set(SLOPPY_DOCTOR_JSON_GOLDEN tests/golden/cli/doctor-json-v8.json)
        set(SLOPPY_DOCTOR_PARTIAL_TEXT_GOLDEN tests/golden/cli/doctor-partial-text-v8.txt)
        set(SLOPPY_DOCTOR_PARTIAL_JSON_GOLDEN tests/golden/cli/doctor-partial-json-v8.json)
        set(SLOPPY_DOCTOR_FS_TEXT_GOLDEN tests/golden/cli/doctor-filesystem-text-v8.txt)
        set(SLOPPY_DOCTOR_FS_JSON_GOLDEN tests/golden/cli/doctor-filesystem-json-v8.json)
        set(SLOPPY_DOCTOR_NET_TEXT_GOLDEN tests/golden/cli/doctor-network-text-v8.txt)
        set(SLOPPY_DOCTOR_NET_JSON_GOLDEN tests/golden/cli/doctor-network-json-v8.json)
        set(SLOPPY_DOCTOR_CRYPTO_NONCRYPTO_TEXT_GOLDEN
            tests/golden/cli/doctor-crypto-noncrypto-hash-text-v8.txt)
        set(SLOPPY_DOCTOR_CRYPTO_NONCRYPTO_JSON_GOLDEN
            tests/golden/cli/doctor-crypto-noncrypto-hash-json-v8.json)
        set(SLOPPY_DOCTOR_CODEC_CHECKSUM_TEXT_GOLDEN
            tests/golden/cli/doctor-codec-checksum-text-v8.txt)
        set(SLOPPY_DOCTOR_CODEC_CHECKSUM_JSON_GOLDEN
            tests/golden/cli/doctor-codec-checksum-json-v8.json)
    endif()
    sloppy_add_cli_golden_test(
        sloppy.cli.doctor_text ${SLOPPY_DOCTOR_TEXT_GOLDEN} doctor --plan
        tests/fixtures/cli/doctor.plan.json --format text)
    sloppy_add_cli_golden_test(
        sloppy.cli.doctor_json ${SLOPPY_DOCTOR_JSON_GOLDEN} doctor --plan
        tests/fixtures/cli/doctor.plan.json --format json)
    sloppy_add_cli_golden_test(
        sloppy.cli.doctor_partial_text ${SLOPPY_DOCTOR_PARTIAL_TEXT_GOLDEN} doctor --plan
        compiler/tests/fixtures/partial-body-without-schema/expected/app.plan.json --format text)
    sloppy_add_cli_golden_test(
        sloppy.cli.doctor_partial_json ${SLOPPY_DOCTOR_PARTIAL_JSON_GOLDEN} doctor --plan
        compiler/tests/fixtures/partial-body-without-schema/expected/app.plan.json --format json)
    sloppy_add_cli_golden_test(
        sloppy.cli.doctor_filesystem_text ${SLOPPY_DOCTOR_FS_TEXT_GOLDEN} doctor --plan
        tests/fixtures/cli/filesystem-policy.plan.json --format text)
    sloppy_add_cli_golden_test(
        sloppy.cli.doctor_filesystem_json ${SLOPPY_DOCTOR_FS_JSON_GOLDEN} doctor --plan
        tests/fixtures/cli/filesystem-policy.plan.json --format json)
    sloppy_add_cli_golden_test(
        sloppy.cli.doctor_network_text ${SLOPPY_DOCTOR_NET_TEXT_GOLDEN} doctor --plan
        tests/fixtures/cli/network-policy.plan.json --format text)
    sloppy_add_cli_golden_test(
        sloppy.cli.doctor_network_json ${SLOPPY_DOCTOR_NET_JSON_GOLDEN} doctor --plan
        tests/fixtures/cli/network-policy.plan.json --format json)
    sloppy_add_cli_golden_test(
        sloppy.cli.doctor_crypto_noncrypto_hash_text
        ${SLOPPY_DOCTOR_CRYPTO_NONCRYPTO_TEXT_GOLDEN} doctor --plan
        tests/fixtures/cli/crypto-noncrypto-hash.plan.json --format text)
    sloppy_add_cli_golden_test(
        sloppy.cli.doctor_crypto_noncrypto_hash_json
        ${SLOPPY_DOCTOR_CRYPTO_NONCRYPTO_JSON_GOLDEN} doctor --plan
        tests/fixtures/cli/crypto-noncrypto-hash.plan.json --format json)
    sloppy_add_cli_golden_test(
        sloppy.cli.doctor_codec_checksum_text
        ${SLOPPY_DOCTOR_CODEC_CHECKSUM_TEXT_GOLDEN} doctor --plan
        tests/fixtures/cli/codec-checksum.plan.json --format text)
    sloppy_add_cli_golden_test(
        sloppy.cli.doctor_codec_checksum_json
        ${SLOPPY_DOCTOR_CODEC_CHECKSUM_JSON_GOLDEN} doctor --plan
        tests/fixtures/cli/codec-checksum.plan.json --format json)
    sloppy_add_cli_nonzero_golden_test(
        sloppy.cli.audit_text tests/golden/cli/audit-text.txt audit --plan
        tests/fixtures/cli/audit-problems.plan.json --format text)
    sloppy_add_cli_nonzero_golden_test(
        sloppy.cli.audit_json tests/golden/cli/audit-json.json audit --plan
        tests/fixtures/cli/audit-problems.plan.json --format json)
    sloppy_add_cli_golden_test(
        sloppy.cli.audit_partial_json tests/golden/cli/audit-partial-json.json audit --plan
        compiler/tests/fixtures/partial-body-without-schema/expected/app.plan.json --format json)
    sloppy_add_cli_golden_test(
        sloppy.cli.audit_clean_text tests/golden/cli/audit-clean-text.txt audit --plan
        tests/fixtures/cli/route-metadata.plan.json --format text)
    sloppy_add_cli_golden_test(
        sloppy.cli.audit_clean_json tests/golden/cli/audit-clean-json.json audit --plan
        tests/fixtures/cli/route-metadata.plan.json --format json)
    sloppy_add_cli_golden_test(
        sloppy.cli.audit_filesystem_text tests/golden/cli/audit-filesystem-text.txt audit --plan
        tests/fixtures/cli/filesystem-policy.plan.json --format text)
    sloppy_add_cli_golden_test(
        sloppy.cli.audit_filesystem_json tests/golden/cli/audit-filesystem-json.json audit --plan
        tests/fixtures/cli/filesystem-policy.plan.json --format json)
    sloppy_add_cli_golden_test(
        sloppy.cli.audit_network_text tests/golden/cli/audit-network-text.txt audit --plan
        tests/fixtures/cli/network-policy.plan.json --format text)
    sloppy_add_cli_golden_test(
        sloppy.cli.audit_network_json tests/golden/cli/audit-network-json.json audit --plan
        tests/fixtures/cli/network-policy.plan.json --format json)
    sloppy_add_cli_golden_test(
        sloppy.cli.openapi_json tests/golden/cli/openapi.json openapi --plan
        tests/fixtures/cli/route-metadata.plan.json)
    sloppy_add_cli_golden_test(
        sloppy.cli.openapi_users_json tests/golden/cli/openapi-users.json openapi --plan
        compiler/tests/fixtures/realistic-users-api/expected/app.plan.json)
    sloppy_add_cli_golden_test(
        sloppy.cli.openapi_partial_json tests/golden/cli/openapi-partial.json openapi --plan
        compiler/tests/fixtures/partial-body-without-schema/expected/app.plan.json)
    sloppy_add_cli_golden_test(
        sloppy.cli.openapi_scalar_schema_json tests/golden/cli/openapi-scalar-schema.json openapi
        --plan tests/fixtures/cli/openapi-scalar-schema.plan.json)
    sloppy_add_cli_golden_test(
        sloppy.cli.openapi_missing_schema_json tests/golden/cli/openapi-missing-schema.json openapi
        --plan tests/fixtures/cli/openapi-missing-schema.plan.json)

    add_test(NAME sloppy.cli.help_includes_run COMMAND "$<TARGET_FILE:sloppy>" --help)
    set_tests_properties(sloppy.cli.help_includes_run PROPERTIES PASS_REGULAR_EXPRESSION "sloppy run")

    add_test(
        NAME docs.main_contract
        COMMAND
            "${CMAKE_COMMAND}" "-DPROJECT_SOURCE_DIR=${PROJECT_SOURCE_DIR}" -P
            "${PROJECT_SOURCE_DIR}/tests/cmake/check_main_contract_docs.cmake")
    add_test(
        NAME docs.engine19_conformance_matrix
        COMMAND
            "${CMAKE_COMMAND}" "-DPROJECT_SOURCE_DIR=${PROJECT_SOURCE_DIR}" -P
            "${PROJECT_SOURCE_DIR}/tests/cmake/check_engine19_conformance_matrix_docs.cmake")

    if(CARGO_EXECUTABLE AND SLOPPY_BUILD_COMPILER)
        function(sloppy_add_conformance_compile_test test_name case_name source_path)
            add_test(
                NAME ${test_name}
                COMMAND
                    "${CMAKE_COMMAND}" "-DPROJECT_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
                    "-DCMAKE_BINARY_DIR=${CMAKE_BINARY_DIR}"
                    "-DCARGO_EXECUTABLE=${CARGO_EXECUTABLE}"
                    "-DSLOPPY_CONFORMANCE_CASE=${case_name}" "-DSLOPPY_SOURCE=${source_path}"
                    -P "${PROJECT_SOURCE_DIR}/tests/cmake/check_conformance_compile_artifacts.cmake")
            set_tests_properties(${test_name} PROPERTIES LABELS "conformance")
        endfunction()

        function(sloppy_add_conformance_rejected_test test_name case_name source_path expected_error)
            add_test(
                NAME ${test_name}
                COMMAND
                    "${CMAKE_COMMAND}" "-DPROJECT_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
                    "-DCMAKE_BINARY_DIR=${CMAKE_BINARY_DIR}"
                    "-DCARGO_EXECUTABLE=${CARGO_EXECUTABLE}"
                    "-DSLOPPY_CONFORMANCE_CASE=${case_name}" "-DSLOPPY_SOURCE=${source_path}"
                    "-DSLOPPY_EXPECTED_ERROR=${expected_error}" -P
                    "${PROJECT_SOURCE_DIR}/tests/cmake/check_conformance_compile_rejected.cmake")
            set_tests_properties(${test_name} PROPERTIES LABELS "conformance")
        endfunction()

        sloppy_add_conformance_compile_test(
            conformance.hello.compile_artifacts hello examples/compiler-hello/app.js)
        sloppy_add_conformance_compile_test(
            conformance.hello_minimal.compile_artifacts hello-minimal
            examples/hello-minimal/app.ts)
        sloppy_add_conformance_compile_test(
            conformance.configured_api.compile_artifacts configured-api
            examples/configured-api/app.js)
        sloppy_add_conformance_compile_test(
            conformance.modules_api.compile_artifacts modules-api examples/modules-api/app.js)
        sloppy_add_conformance_compile_test(
            conformance.validation_errors.compile_artifacts validation-errors
            examples/validation-errors/app.js)
        sloppy_add_conformance_compile_test(
            conformance.request_context.compile_artifacts request-context
            examples/request-context/app.js)
        sloppy_add_conformance_compile_test(
            conformance.http_methods.compile_artifacts http-methods
            compiler/tests/fixtures/http-methods/input.js)
        sloppy_add_conformance_compile_test(
            conformance.async_handler.compile_artifacts async-handler
            compiler/tests/fixtures/async-handler/input.js)
        sloppy_add_conformance_compile_test(
            conformance.provider_capability.compile_artifacts provider-capability
            compiler/tests/fixtures/provider-capability/input.js)
        sloppy_add_conformance_compile_test(
            conformance.users_api_sqlite.compile_artifacts users-api-sqlite
            examples/users-api-sqlite/app.js)
        sloppy_add_conformance_compile_test(
            conformance.source_map.compile_artifacts source-map
            compiler/tests/fixtures/source-map/input.js)
        sloppy_add_conformance_rejected_test(
            conformance.unsupported.dynamic_route dynamic-route
            compiler/tests/fixtures/unsupported-dynamic-route/input.js
            SLOPPYC_E_UNSUPPORTED_DYNAMIC_ROUTE_PATTERN)
        sloppy_add_conformance_rejected_test(
            conformance.unsupported.bare_import bare-import
            compiler/tests/fixtures/unsupported-import-specifier/input.js
            SLOPPYC_E_UNSUPPORTED_IMPORT_SPECIFIER)
        sloppy_add_conformance_rejected_test(
            conformance.unsupported.async_handler_body async-handler-body
            compiler/tests/fixtures/unsupported-async-handler-body/input.js
            SLOPPYC_E_UNSUPPORTED_ASYNC_HANDLER_BODY)
        sloppy_add_conformance_rejected_test(
            conformance.unsupported.secret_capability secret-capability
            compiler/tests/fixtures/unsupported-secret-capability/input.js
            SLOPPYC_E_SECRET_PLAN_METADATA)

        function(sloppy_add_example_tooling_test test_name case_name source_path expected_plan
                 expected_routes expected_capabilities expected_audit expected_openapi)
            set(tooling_command
                "${CMAKE_COMMAND}"
                "-DPROJECT_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
                "-DCMAKE_BINARY_DIR=${CMAKE_BINARY_DIR}"
                "-DCARGO_EXECUTABLE=${CARGO_EXECUTABLE}"
                "-DSLOPPY_CLI=$<TARGET_FILE:sloppy>"
                "-DSLOPPY_CASE=${case_name}"
                "-DSLOPPY_SOURCE=${source_path}")

            if(NOT "${expected_plan}" STREQUAL "")
                list(APPEND tooling_command "-DSLOPPY_EXPECTED_PLAN=${expected_plan}")
            endif()
            if(NOT "${expected_routes}" STREQUAL "")
                list(APPEND tooling_command "-DSLOPPY_EXPECTED_ROUTES=${expected_routes}")
            endif()
            if(NOT "${expected_capabilities}" STREQUAL "")
                list(APPEND tooling_command
                     "-DSLOPPY_EXPECTED_CAPABILITIES=${expected_capabilities}")
            endif()
            if(NOT "${expected_audit}" STREQUAL "")
                list(APPEND tooling_command "-DSLOPPY_EXPECTED_AUDIT=${expected_audit}")
            endif()
            if(NOT "${expected_openapi}" STREQUAL "")
                list(APPEND tooling_command "-DSLOPPY_EXPECTED_OPENAPI=${expected_openapi}")
            endif()
            list(APPEND tooling_command -P
                 "${PROJECT_SOURCE_DIR}/tests/cmake/check_example_tooling.cmake")

            add_test(NAME ${test_name} COMMAND ${tooling_command})
            set_tests_properties(${test_name} PROPERTIES LABELS "conformance;examples")
        endfunction()

        sloppy_add_example_tooling_test(
            examples.hello_minimal.tooling hello-minimal examples/hello-minimal/app.ts
            "Hello.Get" "/hello/\\{name\\}" "" "\"findings\"" "x-slop-completeness")
        sloppy_add_example_tooling_test(
            examples.configured_api.tooling configured-api examples/configured-api/app.js
            "App:Greeting" "Config.Get" "" "\"findings\"" "/config")
        sloppy_add_example_tooling_test(
            examples.modules_api.tooling modules-api examples/modules-api/app.js
            "modules" "Users.Get" "" "\"findings\"" "/users/\\{id\\}")
        sloppy_add_example_tooling_test(
            examples.validation_errors.tooling validation-errors examples/validation-errors/app.js
            "UserCreate" "Users.Create" "" "\"findings\"" "UserCreate")
        sloppy_add_example_tooling_test(
            examples.users_api_sqlite.tooling users-api-sqlite examples/users-api-sqlite/app.js
            "users-api-sqlite-runtime.db" "Users.Create" "data.main" "\"findings\""
            "x-slop-capabilities")
    endif()

    add_test(
        NAME sloppy.cli.missing_metadata
        COMMAND
            "${CMAKE_COMMAND}" "-DSLOPPY_CLI=$<TARGET_FILE:sloppy>"
            "-DSLOPPY_CLI_ARGS=routes;--plan;tests/fixtures/cli/missing.plan.json" -P
            "${PROJECT_SOURCE_DIR}/tests/cmake/check_cli_failure.cmake")
    set_tests_properties(sloppy.cli.missing_metadata PROPERTIES WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}")

    add_test(
        NAME sloppy.cli.openapi_malformed_route
        COMMAND
            "${CMAKE_COMMAND}" "-DSLOPPY_CLI=$<TARGET_FILE:sloppy>"
            "-DSLOPPY_CLI_ARGS=openapi;--plan;tests/fixtures/cli/malformed-route.plan.json"
            "-DSLOPPY_EXPECTED_ERROR=invalid app plan route pattern" -P
            "${PROJECT_SOURCE_DIR}/tests/cmake/check_cli_failure.cmake")
    set_tests_properties(sloppy.cli.openapi_malformed_route
                         PROPERTIES WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}")

    add_test(
        NAME sloppy.run.missing_artifacts
        COMMAND
            "${CMAKE_COMMAND}" "-DSLOPPY_CLI=$<TARGET_FILE:sloppy>"
            "-DSLOPPY_CLI_ARGS=run;--artifacts;tests/fixtures/run/missing;--once;GET;/"
            "-DSLOPPY_EXPECTED_ERROR=artifact path not found" -P
            "${PROJECT_SOURCE_DIR}/tests/cmake/check_cli_failure.cmake")
    set_tests_properties(sloppy.run.missing_artifacts PROPERTIES WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}")

    add_test(
        NAME sloppy.run.missing_plan
        COMMAND
            "${CMAKE_COMMAND}" "-DSLOPPY_CLI=$<TARGET_FILE:sloppy>"
            "-DSLOPPY_CLI_ARGS=run;--artifacts;tests/fixtures/run/missing-plan;--once;GET;/"
            "-DSLOPPY_EXPECTED_ERROR=app.plan.json" -P
            "${PROJECT_SOURCE_DIR}/tests/cmake/check_cli_failure.cmake")
    set_tests_properties(sloppy.run.missing_plan PROPERTIES WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}")

    add_test(
        NAME sloppy.run.malformed_plan
        COMMAND
            "${CMAKE_COMMAND}" "-DSLOPPY_CLI=$<TARGET_FILE:sloppy>"
            "-DSLOPPY_CLI_ARGS=run;--artifacts;tests/fixtures/run/malformed;--once;GET;/"
            "-DSLOPPY_EXPECTED_ERROR=malformed app.plan.json" -P
            "${PROJECT_SOURCE_DIR}/tests/cmake/check_cli_failure.cmake")
    set_tests_properties(sloppy.run.malformed_plan PROPERTIES WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}")

    add_test(
        NAME sloppy.run.invalid_bundle_path
        COMMAND
            "${CMAKE_COMMAND}" "-DSLOPPY_CLI=$<TARGET_FILE:sloppy>"
            "-DSLOPPY_CLI_ARGS=run;--artifacts;tests/fixtures/run/invalid-bundle-path;--once;GET;/"
            "-DSLOPPY_EXPECTED_ERROR=invalid bundle path in app.plan.json" -P
            "${PROJECT_SOURCE_DIR}/tests/cmake/check_cli_failure.cmake")
    set_tests_properties(sloppy.run.invalid_bundle_path
                         PROPERTIES WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}")

    add_test(
        NAME sloppy.run.invalid_source_map_path
        COMMAND
            "${CMAKE_COMMAND}" "-DSLOPPY_CLI=$<TARGET_FILE:sloppy>"
            "-DSLOPPY_CLI_ARGS=run;--artifacts;tests/fixtures/run/invalid-source-map-path;--once;GET;/"
            "-DSLOPPY_EXPECTED_ERROR=invalid source map path in app.plan.json" -P
            "${PROJECT_SOURCE_DIR}/tests/cmake/check_cli_failure.cmake")
    set_tests_properties(sloppy.run.invalid_source_map_path
                         PROPERTIES WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}")

    add_test(
        NAME sloppy.run.hash_mismatch
        COMMAND
            "${CMAKE_COMMAND}" "-DSLOPPY_CLI=$<TARGET_FILE:sloppy>"
            "-DSLOPPY_CLI_ARGS=run;--artifacts;tests/fixtures/run/hash-mismatch;--once;GET;/"
            "-DSLOPPY_EXPECTED_ERROR=bundle hash mismatch" -P
            "${PROJECT_SOURCE_DIR}/tests/cmake/check_cli_failure.cmake")
    set_tests_properties(sloppy.run.hash_mismatch PROPERTIES WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}")

    add_test(
        NAME sloppy.run.source_map_hash_mismatch
        COMMAND
            "${CMAKE_COMMAND}" "-DSLOPPY_CLI=$<TARGET_FILE:sloppy>"
            "-DSLOPPY_CLI_ARGS=run;--artifacts;tests/fixtures/run/source-map-hash-mismatch;--once;GET;/"
            "-DSLOPPY_EXPECTED_ERROR=source map hash mismatch" -P
            "${PROJECT_SOURCE_DIR}/tests/cmake/check_cli_failure.cmake")
    set_tests_properties(sloppy.run.source_map_hash_mismatch
                         PROPERTIES WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}")

    add_test(
        NAME sloppy.run.missing_source_map
        COMMAND
            "${CMAKE_COMMAND}" "-DSLOPPY_CLI=$<TARGET_FILE:sloppy>"
            "-DSLOPPY_CLI_ARGS=run;--artifacts;tests/fixtures/run/missing-source-map;--once;GET;/"
            "-DSLOPPY_EXPECTED_ERROR=artifact path not found" -P
            "${PROJECT_SOURCE_DIR}/tests/cmake/check_cli_failure.cmake")
    set_tests_properties(sloppy.run.missing_source_map
                         PROPERTIES WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}")

    add_test(
        NAME sloppy.run.too_many_routes
        COMMAND
            "${CMAKE_COMMAND}" "-DSLOPPY_CLI=$<TARGET_FILE:sloppy>"
            "-DSLOPPY_CLI_ARGS=run;--artifacts;tests/fixtures/run/too-many-routes;--once;GET;/"
            "-DSLOPPY_EXPECTED_ERROR=app plan has too many runnable routes"
            -P "${PROJECT_SOURCE_DIR}/tests/cmake/check_cli_failure.cmake")
    set_tests_properties(sloppy.run.too_many_routes
                         PROPERTIES WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}")

    add_test(
        NAME sloppy.run.version_mismatch
        COMMAND
            "${CMAKE_COMMAND}" "-DSLOPPY_CLI=$<TARGET_FILE:sloppy>"
            "-DSLOPPY_CLI_ARGS=run;--artifacts;tests/fixtures/run/version-mismatch;--once;GET;/"
            "-DSLOPPY_EXPECTED_ERROR=unsupported app.plan.json runtimeMinimumVersion" -P
            "${PROJECT_SOURCE_DIR}/tests/cmake/check_cli_failure.cmake")
    set_tests_properties(sloppy.run.version_mismatch
                         PROPERTIES WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}")

    add_test(
        NAME sloppy.run.rejects_mixed_artifact_inputs
        COMMAND
            "${CMAKE_COMMAND}" "-DSLOPPY_CLI=$<TARGET_FILE:sloppy>"
            "-DSLOPPY_CLI_ARGS=run;--artifacts;tests/integration/execution/compiler_mvp;extra;--once;GET;/"
            "-DSLOPPY_EXPECTED_ERROR=expected either --artifacts <dir> or one positional artifact path"
            -P "${PROJECT_SOURCE_DIR}/tests/cmake/check_cli_failure.cmake")
    set_tests_properties(sloppy.run.rejects_mixed_artifact_inputs
                         PROPERTIES WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}")

    add_test(
        NAME sloppy.run.rejects_environment_with_artifacts
        COMMAND
            "${CMAKE_COMMAND}" "-DSLOPPY_CLI=$<TARGET_FILE:sloppy>"
            "-DSLOPPY_CLI_ARGS=run;--artifacts;tests/integration/execution/compiler_mvp;--environment;Development;--once;GET;/"
            "-DSLOPPY_EXPECTED_ERROR=--environment only applies to source input or sloppy.json"
            -P "${PROJECT_SOURCE_DIR}/tests/cmake/check_cli_failure.cmake")
    set_tests_properties(sloppy.run.rejects_environment_with_artifacts
                         PROPERTIES WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}")

    if(CARGO_EXECUTABLE AND SLOPPY_BUILD_COMPILER)
        set(SLOPPYC_BUILT_EXECUTABLE
            "${PROJECT_SOURCE_DIR}/compiler/target/debug/sloppyc${CMAKE_EXECUTABLE_SUFFIX}")

        if(NOT SLOPPY_ENABLE_V8)
            add_test(
                NAME sloppy.run.source_input_emits_artifacts_non_v8
                COMMAND
                    "${CMAKE_COMMAND}" "-DPROJECT_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
                    "-DCMAKE_BINARY_DIR=${CMAKE_BINARY_DIR}" "-DSLOPPY_CLI=$<TARGET_FILE:sloppy>"
                    "-DSLOPPYC_EXECUTABLE=${SLOPPYC_BUILT_EXECUTABLE}"
                    "-DSLOPPY_CASE=hello-non-v8" "-DSLOPPY_SOURCE=examples/compiler-hello/app.js"
                    "-DSLOPPY_ONCE_METHOD=GET" "-DSLOPPY_ONCE_TARGET=/"
                    "-DSLOPPY_EXPECTED_ERROR=requires V8-enabled build"
                    "-DSLOPPY_EXPECTED_ARTIFACT_DIR=.sloppy/cache/dev/source-input" -P
                    "${PROJECT_SOURCE_DIR}/tests/cmake/check_source_input_run.cmake")
            set_tests_properties(sloppy.run.source_input_emits_artifacts_non_v8
                                 PROPERTIES LABELS "source-input")

            add_test(
                NAME sloppy.run.project_config_emits_artifacts_non_v8
                COMMAND
                    "${CMAKE_COMMAND}" "-DPROJECT_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
                    "-DCMAKE_BINARY_DIR=${CMAKE_BINARY_DIR}" "-DSLOPPY_CLI=$<TARGET_FILE:sloppy>"
                    "-DSLOPPYC_EXECUTABLE=${SLOPPYC_BUILT_EXECUTABLE}"
                    "-DSLOPPY_CASE=config-non-v8"
                    "-DSLOPPY_CONFIG_ENTRY={\"entry\":\"app.js\",\"outDir\":\".sloppy\",\"environment\":\"Development\"}"
                    "-DSLOPPY_CONFIG_SOURCE=examples/compiler-hello/app.js"
                    "-DSLOPPY_ONCE_METHOD=GET" "-DSLOPPY_ONCE_TARGET=/"
                    "-DSLOPPY_EXPECTED_ERROR=requires V8-enabled build"
                    "-DSLOPPY_EXPECTED_ARTIFACT_DIR=.sloppy" -P
                    "${PROJECT_SOURCE_DIR}/tests/cmake/check_source_input_run.cmake")
            set_tests_properties(sloppy.run.project_config_emits_artifacts_non_v8
                                 PROPERTIES LABELS "source-input")

            add_test(
                NAME sloppy.run.project_config_shell_metacharacters_non_v8
                COMMAND
                    "${CMAKE_COMMAND}" "-DPROJECT_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
                    "-DCMAKE_BINARY_DIR=${CMAKE_BINARY_DIR}" "-DSLOPPY_CLI=$<TARGET_FILE:sloppy>"
                    "-DSLOPPYC_EXECUTABLE=${SLOPPYC_BUILT_EXECUTABLE}"
                    "-DSLOPPY_CASE=config-metacharacters-non-v8"
                    "-DSLOPPY_CONFIG_ENTRY={\"entry\":\"app.js\",\"outDir\":\".sloppy/$(not-a-command)\",\"environment\":\"Development\"}"
                    "-DSLOPPY_CONFIG_SOURCE=examples/compiler-hello/app.js"
                    "-DSLOPPY_ONCE_METHOD=GET" "-DSLOPPY_ONCE_TARGET=/"
                    "-DSLOPPY_EXPECTED_ERROR=requires V8-enabled build"
                    "-DSLOPPY_EXPECTED_ARTIFACT_DIR=.sloppy/$(not-a-command)" -P
                    "${PROJECT_SOURCE_DIR}/tests/cmake/check_source_input_run.cmake")
            set_tests_properties(sloppy.run.project_config_shell_metacharacters_non_v8
                                 PROPERTIES LABELS "source-input")

            add_test(
                NAME sloppy.run.source_input_config_driven_sqlite_non_v8
                COMMAND
                    "${CMAKE_COMMAND}" "-DPROJECT_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
                    "-DCMAKE_BINARY_DIR=${CMAKE_BINARY_DIR}" "-DSLOPPY_CLI=$<TARGET_FILE:sloppy>"
                    "-DSLOPPYC_EXECUTABLE=${SLOPPYC_BUILT_EXECUTABLE}"
                    "-DSLOPPY_CASE=config-sqlite-non-v8"
                    "-DSLOPPY_SOURCE=examples/users-api-sqlite/app.js" "-DSLOPPY_ONCE_METHOD=GET"
                    "-DSLOPPY_ONCE_TARGET=/users"
                    "-DSLOPPY_ENVIRONMENT=Development"
                    "-DSLOPPY_EXPECTED_ERROR=requires V8-enabled build"
                    "-DSLOPPY_EXPECTED_ARTIFACT_DIR=.sloppy/cache/dev/source-input"
                    "-DSLOPPY_EXPECTED_PLAN=users-api-sqlite-runtime.db" -P
                    "${PROJECT_SOURCE_DIR}/tests/cmake/check_source_input_run.cmake")
            set_tests_properties(sloppy.run.source_input_config_driven_sqlite_non_v8
                                 PROPERTIES LABELS "source-input")
            add_test(
                NAME sloppy.run.hello_minimal_source_input_non_v8
                COMMAND
                    "${CMAKE_COMMAND}" "-DPROJECT_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
                    "-DCMAKE_BINARY_DIR=${CMAKE_BINARY_DIR}" "-DSLOPPY_CLI=$<TARGET_FILE:sloppy>"
                    "-DSLOPPYC_EXECUTABLE=${SLOPPYC_BUILT_EXECUTABLE}"
                    "-DSLOPPY_CASE=hello-minimal-non-v8"
                    "-DSLOPPY_SOURCE=examples/hello-minimal/app.ts" "-DSLOPPY_ONCE_METHOD=GET"
                    "-DSLOPPY_ONCE_TARGET=/hello/Ada"
                    "-DSLOPPY_EXPECTED_ERROR=requires V8-enabled build"
                    "-DSLOPPY_EXPECTED_ARTIFACT_DIR=.sloppy/cache/dev/source-input"
                    "-DSLOPPY_EXPECTED_PLAN=Hello.Get" -P
                    "${PROJECT_SOURCE_DIR}/tests/cmake/check_source_input_run.cmake")
            set_tests_properties(sloppy.run.hello_minimal_source_input_non_v8
                                 PROPERTIES LABELS "source-input")
        endif()

        add_test(
            NAME sloppy.run.source_input_missing_source
            COMMAND
                "${CMAKE_COMMAND}" "-DPROJECT_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
                "-DCMAKE_BINARY_DIR=${CMAKE_BINARY_DIR}" "-DSLOPPY_CLI=$<TARGET_FILE:sloppy>"
                "-DSLOPPYC_EXECUTABLE=${SLOPPYC_BUILT_EXECUTABLE}"
                "-DSLOPPY_CASE=missing-source" "-DSLOPPY_SOURCE=tests/fixtures/run/missing.js"
                "-DSLOPPY_EXPECTED_ERROR=missing source file" -P
                "${PROJECT_SOURCE_DIR}/tests/cmake/check_source_input_run.cmake")
        set_tests_properties(sloppy.run.source_input_missing_source
                             PROPERTIES LABELS "source-input")

        add_test(
            NAME sloppy.run.source_input_invalid_config
            COMMAND
                "${CMAKE_COMMAND}" "-DPROJECT_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
                "-DCMAKE_BINARY_DIR=${CMAKE_BINARY_DIR}" "-DSLOPPY_CLI=$<TARGET_FILE:sloppy>"
                "-DSLOPPYC_EXECUTABLE=${SLOPPYC_BUILT_EXECUTABLE}"
                "-DSLOPPY_CASE=invalid-config" "-DSLOPPY_CONFIG_ENTRY={"
                "-DSLOPPY_EXPECTED_ERROR=invalid sloppy.json" -P
                "${PROJECT_SOURCE_DIR}/tests/cmake/check_source_input_run.cmake")
        set_tests_properties(sloppy.run.source_input_invalid_config
                             PROPERTIES LABELS "source-input")

        add_test(
            NAME sloppy.run.source_input_missing_entry
            COMMAND
                "${CMAKE_COMMAND}" "-DPROJECT_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
                "-DCMAKE_BINARY_DIR=${CMAKE_BINARY_DIR}" "-DSLOPPY_CLI=$<TARGET_FILE:sloppy>"
                "-DSLOPPYC_EXECUTABLE=${SLOPPYC_BUILT_EXECUTABLE}"
                "-DSLOPPY_CASE=missing-entry" "-DSLOPPY_CONFIG_ENTRY={\"outDir\":\".sloppy\"}"
                "-DSLOPPY_EXPECTED_ERROR=missing entry" -P
                "${PROJECT_SOURCE_DIR}/tests/cmake/check_source_input_run.cmake")
        set_tests_properties(sloppy.run.source_input_missing_entry
                             PROPERTIES LABELS "source-input")

        add_test(
            NAME sloppy.run.source_input_invalid_entry
            COMMAND
                "${CMAKE_COMMAND}" "-DPROJECT_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
                "-DCMAKE_BINARY_DIR=${CMAKE_BINARY_DIR}" "-DSLOPPY_CLI=$<TARGET_FILE:sloppy>"
                "-DSLOPPYC_EXECUTABLE=${SLOPPYC_BUILT_EXECUTABLE}"
                "-DSLOPPY_CASE=invalid-entry" "-DSLOPPY_CONFIG_ENTRY={\"entry\":123}"
                "-DSLOPPY_EXPECTED_ERROR=entry must be a non-empty string" -P
                "${PROJECT_SOURCE_DIR}/tests/cmake/check_source_input_run.cmake")
        set_tests_properties(sloppy.run.source_input_invalid_entry
                             PROPERTIES LABELS "source-input")

        add_test(
            NAME sloppy.run.source_input_compiler_failure
            COMMAND
                "${CMAKE_COMMAND}" "-DPROJECT_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
                "-DCMAKE_BINARY_DIR=${CMAKE_BINARY_DIR}" "-DSLOPPY_CLI=$<TARGET_FILE:sloppy>"
                "-DSLOPPYC_EXECUTABLE=${SLOPPYC_BUILT_EXECUTABLE}"
                "-DSLOPPY_CASE=compiler-failure"
                "-DSLOPPY_SOURCE=compiler/tests/fixtures/computed-method/input.js"
                "-DSLOPPY_EXPECTED_ERROR=compiler handoff failed" -P
                "${PROJECT_SOURCE_DIR}/tests/cmake/check_source_input_run.cmake")
        set_tests_properties(sloppy.run.source_input_compiler_failure
                             PROPERTIES LABELS "source-input")

        add_test(
            NAME sloppy.run.source_input_unsupported_typescript
            COMMAND
                "${CMAKE_COMMAND}" "-DPROJECT_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
                "-DCMAKE_BINARY_DIR=${CMAKE_BINARY_DIR}" "-DSLOPPY_CLI=$<TARGET_FILE:sloppy>"
                "-DSLOPPYC_EXECUTABLE=${SLOPPYC_BUILT_EXECUTABLE}"
                "-DSLOPPY_CASE=unsupported-typescript"
                "-DSLOPPY_SOURCE=compiler/tests/fixtures/unsupported-typescript-handler/input.ts"
                "-DSLOPPY_EXPECTED_ERROR=UNSUPPORTED_TYPESCRIPT_HANDLER" -P
                "${PROJECT_SOURCE_DIR}/tests/cmake/check_source_input_run.cmake")
        set_tests_properties(sloppy.run.source_input_unsupported_typescript
                             PROPERTIES LABELS "source-input")

        add_test(
            NAME sloppy.run.source_input_missing_compiler
            COMMAND
                "${CMAKE_COMMAND}" "-DPROJECT_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
                "-DCMAKE_BINARY_DIR=${CMAKE_BINARY_DIR}" "-DSLOPPY_CLI=$<TARGET_FILE:sloppy>"
                "-DSLOPPYC_EXECUTABLE=${CMAKE_BINARY_DIR}/missing-sloppyc.exe"
                "-DSLOPPY_CASE=missing-compiler"
                "-DSLOPPY_SOURCE=examples/compiler-hello/app.js"
                "-DSLOPPY_EXPECTED_ERROR=compiler unavailable" -P
                "${PROJECT_SOURCE_DIR}/tests/cmake/check_source_input_run.cmake")
        set_tests_properties(sloppy.run.source_input_missing_compiler
                             PROPERTIES LABELS "source-input")
    endif()

    if(NOT SLOPPY_ENABLE_V8)
        add_test(
            NAME sloppy.run.v8_disabled_is_clear
            COMMAND
                "${CMAKE_COMMAND}" "-DSLOPPY_CLI=$<TARGET_FILE:sloppy>"
                "-DSLOPPY_CLI_ARGS=run;--artifacts;tests/integration/execution/compiler_mvp;--once;GET;/"
                "-DSLOPPY_EXPECT_NO_STDLIB_FS=1"
                "-DSLOPPY_EXPECTED_ERROR=requires V8-enabled build" -P
                "${PROJECT_SOURCE_DIR}/tests/cmake/check_cli_failure.cmake")
        set_tests_properties(sloppy.run.v8_disabled_is_clear
                             PROPERTIES WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}")
        add_test(
            NAME sloppy.run.runtime_artifacts_without_fs_feature_non_v8
            COMMAND
                "${CMAKE_COMMAND}" "-DSLOPPY_CLI=$<TARGET_FILE:sloppy>"
                "-DSLOPPY_CLI_ARGS=run;--artifacts;tests/integration/execution/compiler_mvp;--once;GET;/"
                "-DSLOPPY_EXPECT_NO_STDLIB_FS=1"
                "-DSLOPPY_EXPECTED_ERROR=requires V8-enabled build" -P
                "${PROJECT_SOURCE_DIR}/tests/cmake/check_cli_failure.cmake")
        set_tests_properties(
            sloppy.run.runtime_artifacts_without_fs_feature_non_v8
            PROPERTIES WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
                       LABELS "runtime-artifact;filesystem-boundary")
    else()
        if(CARGO_EXECUTABLE)
            function(sloppy_add_conformance_run_once_test test_name case_name source_path method target
                     expected_output)
                add_test(
                    NAME ${test_name}
                    COMMAND
                        "${CMAKE_COMMAND}" "-DPROJECT_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
                        "-DCMAKE_BINARY_DIR=${CMAKE_BINARY_DIR}"
                        "-DCARGO_EXECUTABLE=${CARGO_EXECUTABLE}" "-DSLOPPY_CLI=$<TARGET_FILE:sloppy>"
                        "-DSLOPPY_CONFORMANCE_CASE=${case_name}" "-DSLOPPY_SOURCE=${source_path}"
                        "-DSLOPPY_ONCE_METHOD=${method}" "-DSLOPPY_ONCE_TARGET=${target}"
                        "-DSLOPPY_EXPECTED_OUTPUT=${expected_output}" -P
                        "${PROJECT_SOURCE_DIR}/tests/cmake/check_conformance_run_once.cmake")
                set_tests_properties(${test_name} PROPERTIES LABELS "conformance;v8")
            endfunction()

            sloppy_add_conformance_run_once_test(
                conformance.hello.run_once hello examples/compiler-hello/app.js GET /
                "Hello from Sloppy")
            sloppy_add_conformance_run_once_test(
                conformance.request_context.run_once request-context examples/request-context/app.js GET
                "/users/123?q=abc&q=last"
                "\"id\":\"123\".*\"q\":\"last\".*\"method\":\"GET\".*\"path\":\"/users/123\".*\"rawTarget\":\"/users/123[?]q=abc&q=last\"")
            sloppy_add_conformance_run_once_test(
                conformance.async_handler.run_once async-handler
                compiler/tests/fixtures/async-handler/input.js GET /async "\"ok\":true")
            if(SLOPPY_BUILD_COMPILER)
                add_test(
                    NAME conformance.hello.source_input_run_once
                    COMMAND
                        "${CMAKE_COMMAND}" "-DPROJECT_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
                        "-DCMAKE_BINARY_DIR=${CMAKE_BINARY_DIR}"
                        "-DSLOPPY_CLI=$<TARGET_FILE:sloppy>"
                        "-DSLOPPYC_EXECUTABLE=${SLOPPYC_BUILT_EXECUTABLE}"
                        "-DSLOPPY_CASE=hello-v8" "-DSLOPPY_SOURCE=examples/compiler-hello/app.js"
                        "-DSLOPPY_ONCE_METHOD=GET" "-DSLOPPY_ONCE_TARGET=/"
                        "-DSLOPPY_EXPECTED_OUTPUT=Hello from Sloppy"
                        "-DSLOPPY_EXPECTED_ARTIFACT_DIR=.sloppy/cache/dev/source-input" -P
                        "${PROJECT_SOURCE_DIR}/tests/cmake/check_source_input_run.cmake")
                set_tests_properties(conformance.hello.source_input_run_once
                                     PROPERTIES LABELS "conformance;v8;source-input")
                add_test(
                    NAME conformance.hello_minimal.source_input_run_once
                    COMMAND
                        "${CMAKE_COMMAND}" "-DPROJECT_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
                        "-DCMAKE_BINARY_DIR=${CMAKE_BINARY_DIR}"
                        "-DSLOPPY_CLI=$<TARGET_FILE:sloppy>"
                        "-DSLOPPYC_EXECUTABLE=${SLOPPYC_BUILT_EXECUTABLE}"
                        "-DSLOPPY_CASE=hello-minimal-v8"
                        "-DSLOPPY_SOURCE=examples/hello-minimal/app.ts"
                        "-DSLOPPY_ONCE_METHOD=GET" "-DSLOPPY_ONCE_TARGET=/hello/Ada"
                        "-DSLOPPY_EXPECTED_OUTPUT=\"hello\":\"Ada\""
                        "-DSLOPPY_EXPECTED_ARTIFACT_DIR=.sloppy/cache/dev/source-input" -P
                        "${PROJECT_SOURCE_DIR}/tests/cmake/check_source_input_run.cmake")
                set_tests_properties(conformance.hello_minimal.source_input_run_once
                                     PROPERTIES LABELS "conformance;v8;source-input")
                add_test(
                    NAME conformance.users_api_sqlite.source_input_run_once
                    COMMAND
                        "${CMAKE_COMMAND}" "-DPROJECT_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
                        "-DCMAKE_BINARY_DIR=${CMAKE_BINARY_DIR}"
                        "-DSLOPPY_CLI=$<TARGET_FILE:sloppy>"
                        "-DSLOPPYC_EXECUTABLE=${SLOPPYC_BUILT_EXECUTABLE}"
                        "-DSLOPPY_CASE=users-api-sqlite-v8"
                        "-DSLOPPY_SOURCE=examples/users-api-sqlite/app.js"
                        "-DSLOPPY_ONCE_METHOD=GET" "-DSLOPPY_ONCE_TARGET=/users"
                        "-DSLOPPY_EXPECTED_OUTPUT=Ada Lovelace"
                        "-DSLOPPY_EXPECTED_ARTIFACT_DIR=.sloppy/cache/dev/source-input" -P
                        "${PROJECT_SOURCE_DIR}/tests/cmake/check_source_input_run.cmake")
                set_tests_properties(conformance.users_api_sqlite.source_input_run_once
                                     PROPERTIES LABELS "conformance;v8;source-input;sqlite")
            endif()
            add_test(
                NAME conformance.users_api_sqlite.localhost_transport
                COMMAND
                    powershell -NoProfile -ExecutionPolicy Bypass -File
                    "${PROJECT_SOURCE_DIR}/tests/scripts/test_users_api_sqlite_transport.ps1"
                    -ProjectSourceDir "${PROJECT_SOURCE_DIR}" -CMakeBinaryDir "${CMAKE_BINARY_DIR}"
                    -CargoExecutable "${CARGO_EXECUTABLE}" -SloppyCli "$<TARGET_FILE:sloppy>")
            set_tests_properties(conformance.users_api_sqlite.localhost_transport
                                 PROPERTIES LABELS "conformance;v8;transport;sqlite;capability")
        endif()

        function(sloppy_add_conformance_existing_artifact_test test_name artifacts method target
                 expected_output)
            add_test(
                NAME ${test_name}
                COMMAND
                    "${CMAKE_COMMAND}" "-DPROJECT_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
                    "-DSLOPPY_CLI=$<TARGET_FILE:sloppy>" "-DSLOPPY_ARTIFACTS=${artifacts}"
                    "-DSLOPPY_ONCE_METHOD=${method}" "-DSLOPPY_ONCE_TARGET=${target}"
                    "-DSLOPPY_EXPECTED_OUTPUT=${expected_output}" -P
                    "${PROJECT_SOURCE_DIR}/tests/cmake/check_conformance_existing_artifacts.cmake")
            set_tests_properties(${test_name} PROPERTIES LABELS "conformance;v8")
        endfunction()

        sloppy_add_conformance_existing_artifact_test(
            conformance.results.invalid_descriptor tests/integration/execution/invalid_descriptor GET /
            "500 Internal Server Error")
        set_tests_properties(
            conformance.results.invalid_descriptor PROPERTIES LABELS "conformance;v8;results")
        sloppy_add_conformance_existing_artifact_test(
            conformance.sqlite.bridge tests/integration/execution/sqlite_bridge GET /sqlite "Ada")
        set_tests_properties(
            conformance.sqlite.bridge PROPERTIES LABELS "conformance;v8;sqlite;capability")
        sloppy_add_conformance_existing_artifact_test(
            conformance.sqlite.denied_capability
            tests/integration/execution/sqlite_denied_capability GET /sqlite-denied
            "500 Internal Server Error")
        set_tests_properties(
            conformance.sqlite.denied_capability
            PROPERTIES LABELS "conformance;v8;sqlite;capability")
        add_test(
            NAME sloppy.run.once_hello
            COMMAND "$<TARGET_FILE:sloppy>" run --artifacts
                    tests/integration/execution/compiler_mvp --once GET /
            WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}")
        set_tests_properties(sloppy.run.once_hello PROPERTIES PASS_REGULAR_EXPRESSION "Hello from Sloppy")
        add_test(
            NAME sloppy.run.once_route_miss
            COMMAND "$<TARGET_FILE:sloppy>" run --artifacts
                    tests/integration/execution/compiler_mvp --once GET /missing
            WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}")
        set_tests_properties(sloppy.run.once_route_miss PROPERTIES PASS_REGULAR_EXPRESSION "404 Not Found")
        add_test(
            NAME sloppy.run.once_unsupported_method
            COMMAND "$<TARGET_FILE:sloppy>" run --artifacts
                    tests/integration/execution/compiler_mvp --once POST /
            WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}")
        set_tests_properties(sloppy.run.once_unsupported_method
                             PROPERTIES PASS_REGULAR_EXPRESSION "405 Method Not Allowed")
        add_test(
            NAME sloppy.run.once_request_context
            COMMAND "$<TARGET_FILE:sloppy>" run --artifacts
                    tests/integration/execution/request_context --once GET /users/123?q=abc
            WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}")
        set_tests_properties(
            sloppy.run.once_request_context
            PROPERTIES PASS_REGULAR_EXPRESSION
                       "\"id\":\"123\".*\"q\":\"abc\".*\"path\":\"/users/123\"")
        add_test(
            NAME sloppy.run.once_sqlite_bridge
            COMMAND "$<TARGET_FILE:sloppy>" run --artifacts
                    tests/integration/execution/sqlite_bridge --once GET /sqlite
            WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}")
        set_tests_properties(
            sloppy.run.once_sqlite_bridge
            PROPERTIES PASS_REGULAR_EXPRESSION "name.*Ada.*name.*Grace")
        add_test(
            NAME sloppy.run.once_sqlite_denied_capability
            COMMAND "$<TARGET_FILE:sloppy>" run --artifacts
                    tests/integration/execution/sqlite_denied_capability --once GET
                    /sqlite-denied
            WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}")
        set_tests_properties(
            sloppy.run.once_sqlite_denied_capability
            PROPERTIES PASS_REGULAR_EXPRESSION "500 Internal Server Error")
        add_test(
            NAME sloppy.run.once_invalid_descriptor
            COMMAND "$<TARGET_FILE:sloppy>" run --artifacts
                    tests/integration/execution/invalid_descriptor --once GET /
            WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}")
        set_tests_properties(sloppy.run.once_invalid_descriptor
                             PROPERTIES PASS_REGULAR_EXPRESSION "500 Internal Server Error")
        add_test(
            NAME sloppy.run.missing_stdlib_asset
            COMMAND
                "${CMAKE_COMMAND}" "-DSLOPPY_CLI=$<TARGET_FILE:sloppy>"
                "-DSLOPPY_CLI_ARGS=run;--artifacts;tests/integration/execution/compiler_mvp;--stdlib;tests/fixtures/run/missing;--once;GET;/"
                "-DSLOPPY_EXPECTED_ERROR=stdlib asset missing" -P
                "${PROJECT_SOURCE_DIR}/tests/cmake/check_cli_failure.cmake")
        set_tests_properties(sloppy.run.missing_stdlib_asset
                             PROPERTIES WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}")
        add_test(
            NAME sloppy.run.missing_app_module
            COMMAND
                "${CMAKE_COMMAND}" "-DSLOPPY_CLI=$<TARGET_FILE:sloppy>"
                "-DSLOPPY_CLI_ARGS=run;--artifacts;tests/fixtures/run/missing-app-module;--once;GET;/"
                "-DSLOPPY_EXPECTED_ERROR=artifact path not found" -P
                "${PROJECT_SOURCE_DIR}/tests/cmake/check_cli_failure.cmake")
        set_tests_properties(sloppy.run.missing_app_module
                             PROPERTIES WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}")
        add_test(
            NAME sloppy.run.missing_handler_registration
            COMMAND
                "${CMAKE_COMMAND}" "-DSLOPPY_CLI=$<TARGET_FILE:sloppy>"
                "-DSLOPPY_CLI_ARGS=run;--artifacts;tests/integration/execution/missing_handler;--once;GET;/"
                "-DSLOPPY_EXPECTED_ERROR=unregistered handler ID" -P
                "${PROJECT_SOURCE_DIR}/tests/cmake/check_cli_failure.cmake")
        set_tests_properties(sloppy.run.missing_handler_registration
                             PROPERTIES WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}")
        add_test(
            NAME sloppy.run.duplicate_handler_registration
            COMMAND
                "${CMAKE_COMMAND}" "-DSLOPPY_CLI=$<TARGET_FILE:sloppy>"
                "-DSLOPPY_CLI_ARGS=run;--artifacts;tests/integration/execution/duplicate_handler;--once;GET;/"
                "-DSLOPPY_EXPECTED_ERROR=duplicate handler ID" -P
                "${PROJECT_SOURCE_DIR}/tests/cmake/check_cli_failure.cmake")
        set_tests_properties(sloppy.run.duplicate_handler_registration
                             PROPERTIES WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}")
    endif()

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
    add_test(
        NAME examples.data_foundation.api_shape
        COMMAND "${CMAKE_COMMAND}" "-DPROJECT_SOURCE_DIR=${PROJECT_SOURCE_DIR}" -P
                "${PROJECT_SOURCE_DIR}/tests/cmake/check_data_foundation_example.cmake")
    add_test(
        NAME examples.sqlite_basic.api_shape
        COMMAND "${CMAKE_COMMAND}" "-DPROJECT_SOURCE_DIR=${PROJECT_SOURCE_DIR}" -P
                "${PROJECT_SOURCE_DIR}/tests/cmake/check_sqlite_basic_example.cmake")
    add_test(
        NAME examples.postgres_basic.api_shape
        COMMAND "${CMAKE_COMMAND}" "-DPROJECT_SOURCE_DIR=${PROJECT_SOURCE_DIR}" -P
                "${PROJECT_SOURCE_DIR}/tests/cmake/check_postgres_basic_example.cmake")
    add_test(
        NAME examples.sqlserver_basic.api_shape
        COMMAND "${CMAKE_COMMAND}" "-DPROJECT_SOURCE_DIR=${PROJECT_SOURCE_DIR}" -P
                "${PROJECT_SOURCE_DIR}/tests/cmake/check_sqlserver_basic_example.cmake")
    add_test(
        NAME examples.request_context.api_shape
        COMMAND "${CMAKE_COMMAND}" "-DPROJECT_SOURCE_DIR=${PROJECT_SOURCE_DIR}" -P
                "${PROJECT_SOURCE_DIR}/tests/cmake/check_request_context_example.cmake")
    add_test(
        NAME examples.time.api_shape
        COMMAND "${CMAKE_COMMAND}" "-DPROJECT_SOURCE_DIR=${PROJECT_SOURCE_DIR}" -P
                "${PROJECT_SOURCE_DIR}/tests/cmake/check_time_examples.cmake")
    add_test(
        NAME examples.crypto.api_shape
        COMMAND "${CMAKE_COMMAND}" "-DPROJECT_SOURCE_DIR=${PROJECT_SOURCE_DIR}" -P
                "${PROJECT_SOURCE_DIR}/tests/cmake/check_crypto_examples.cmake")
    add_test(
        NAME examples.codec.api_shape
        COMMAND "${CMAKE_COMMAND}" "-DPROJECT_SOURCE_DIR=${PROJECT_SOURCE_DIR}" -P
                "${PROJECT_SOURCE_DIR}/tests/cmake/check_codec_examples.cmake")
    add_test(
        NAME examples.net.api_shape
        COMMAND "${CMAKE_COMMAND}" "-DPROJECT_SOURCE_DIR=${PROJECT_SOURCE_DIR}" -P
                "${PROJECT_SOURCE_DIR}/tests/cmake/check_net_examples.cmake")

    if(NODE_EXECUTABLE)
        add_test(
            NAME bootstrap.stdlib.app_host_foundation
            COMMAND "${NODE_EXECUTABLE}"
                    "${PROJECT_SOURCE_DIR}/tests/bootstrap/test_app_host_foundation.mjs")
        add_test(
            NAME bootstrap.stdlib.modules
            COMMAND "${NODE_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tests/bootstrap/test_modules.mjs")
        add_test(
            NAME bootstrap.stdlib.data_foundation
            COMMAND "${NODE_EXECUTABLE}"
                    "${PROJECT_SOURCE_DIR}/tests/bootstrap/test_data_foundation.mjs")
        add_test(
            NAME bootstrap.stdlib.codec
            COMMAND "${NODE_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tests/bootstrap/test_codec.mjs")
        set_tests_properties(
            bootstrap.stdlib.app_host_foundation bootstrap.stdlib.modules
            bootstrap.stdlib.data_foundation bootstrap.stdlib.codec PROPERTIES
            WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}")
    endif()

    if(CARGO_EXECUTABLE)
        add_test(
            NAME sloppyc.cli.version
            COMMAND "${CARGO_EXECUTABLE}" run --manifest-path
                    "${PROJECT_SOURCE_DIR}/compiler/Cargo.toml" -- --version
            WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}")
        set_tests_properties(sloppyc.cli.version PROPERTIES PASS_REGULAR_EXPRESSION "sloppyc")
        add_test(
            NAME sloppyc.supported_app_pipeline
            COMMAND "${CARGO_EXECUTABLE}" test --manifest-path
                    "${PROJECT_SOURCE_DIR}/compiler/Cargo.toml"
            WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}")
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
            COMMENT "Building sloppyc supported app compiler"
            VERBATIM)
    else()
        message(WARNING "Cargo was not found; the sloppyc CMake target will not be available.")
    endif()
endif()

install(TARGETS sloppy RUNTIME DESTINATION bin)
install(DIRECTORY "${SLOPPY_BOOTSTRAP_SOURCE_DIR}" DESTINATION "lib/sloppy/bootstrap")
install(DIRECTORY "${SLOPPY_BOOTSTRAP_SOURCE_DIR}" DESTINATION "lib/sloppy/stdlib")
if(SLOPPY_BUILD_COMPILER AND CARGO_EXECUTABLE)
    set(SLOPPYC_CARGO_PROFILE_DEFAULT "debug")
    if(CMAKE_BUILD_TYPE)
        string(TOLOWER "${CMAKE_BUILD_TYPE}" SLOPPYC_CMAKE_BUILD_TYPE)
        if(SLOPPYC_CMAKE_BUILD_TYPE STREQUAL "release" OR
           SLOPPYC_CMAKE_BUILD_TYPE STREQUAL "relwithdebinfo" OR
           SLOPPYC_CMAKE_BUILD_TYPE STREQUAL "minsizerel")
            set(SLOPPYC_CARGO_PROFILE_DEFAULT "release")
        endif()
    endif()
    set(SLOPPYC_CARGO_PROFILE "${SLOPPYC_CARGO_PROFILE_DEFAULT}" CACHE STRING
        "Cargo profile used for installing sloppyc")
    set(SLOPPYC_INSTALL_PATH
        "${PROJECT_SOURCE_DIR}/compiler/target/${SLOPPYC_CARGO_PROFILE}/sloppyc${CMAKE_EXECUTABLE_SUFFIX}")
    install(PROGRAMS "${SLOPPYC_INSTALL_PATH}" DESTINATION bin)
endif()
