option(SLOPPY_ENABLE_TESTS "Build and register Sloppy tests." ON)
option(SLOPPY_ENABLE_ASSERTS "Enable Sloppy runtime assertions." ON)
option(SLOPPY_ENABLE_WERROR "Treat compiler warnings as errors." OFF)
option(SLOPPY_ENABLE_ASAN "Enable AddressSanitizer where supported." OFF)
option(SLOPPY_ENABLE_UBSAN "Enable UndefinedBehaviorSanitizer where supported." OFF)
option(SLOPPY_ENABLE_TSAN "Enable ThreadSanitizer where supported." OFF)
option(SLOPPY_ENABLE_LIBFUZZER "Build opt-in libFuzzer targets for fuzz/property lanes." OFF)
set(SLOPPY_ENABLE_SIMD
    "AUTO"
    CACHE STRING
          "Enable SIMD backends for canonical scalar primitives: AUTO, ON, or OFF.")
set_property(CACHE SLOPPY_ENABLE_SIMD PROPERTY STRINGS "AUTO" "ON" "OFF")
set(SLOPPY_SIMD_LEVEL
    "AUTO"
    CACHE STRING "Preferred SIMD backend level: AUTO, SSE2, or AVX2.")
set_property(CACHE SLOPPY_SIMD_LEVEL PROPERTY STRINGS "AUTO" "SSE2" "AVX2")
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
set(SLOPPY_PACKAGE_ARCHIVE "" CACHE FILEPATH "Optional Sloppy package archive for package lane smoke tests.")
set(SLOPPYC_CARGO_PROFILE_DEFAULT "debug")
if(CMAKE_BUILD_TYPE)
    string(TOLOWER "${CMAKE_BUILD_TYPE}" SLOPPYC_CMAKE_BUILD_TYPE)
    if(SLOPPYC_CMAKE_BUILD_TYPE STREQUAL "release" OR
       SLOPPYC_CMAKE_BUILD_TYPE STREQUAL "relwithdebinfo" OR
       SLOPPYC_CMAKE_BUILD_TYPE STREQUAL "minsizerel")
        set(SLOPPYC_CARGO_PROFILE_DEFAULT "release")
    endif()
endif()
set(SLOPPYC_CARGO_PROFILE
    "${SLOPPYC_CARGO_PROFILE_DEFAULT}"
    CACHE STRING "Cargo profile used for building and installing sloppyc")
set(SLOPPYC_CARGO_BUILD_PROFILE_ARGS "")
if(SLOPPYC_CARGO_PROFILE STREQUAL "release")
    set(SLOPPYC_CARGO_BUILD_PROFILE_ARGS "--release")
endif()
set(SLOPPYC_BUILT_EXECUTABLE
    "${PROJECT_SOURCE_DIR}/compiler/target/${SLOPPYC_CARGO_PROFILE}/sloppyc${CMAKE_EXECUTABLE_SUFFIX}")
if((SLOPPY_ENABLE_LIBFUZZER OR SLOPPY_ENABLE_ASAN) AND NOT CMAKE_C_COMPILER_ID MATCHES "Clang")
    message(FATAL_ERROR "SLOPPY_ENABLE_LIBFUZZER/SLOPPY_ENABLE_ASAN require a Clang-family C compiler.")
endif()
set(SLOPPY_CLANG_RESOURCE_DIR "")
if((SLOPPY_ENABLE_LIBFUZZER OR SLOPPY_ENABLE_ASAN) AND MSVC)
    execute_process(
        COMMAND "${CMAKE_C_COMPILER}" -print-resource-dir
        OUTPUT_VARIABLE SLOPPY_CLANG_RESOURCE_DIR
        OUTPUT_STRIP_TRAILING_WHITESPACE
        RESULT_VARIABLE SLOPPY_CLANG_RESOURCE_RESULT)
    if(NOT SLOPPY_CLANG_RESOURCE_RESULT EQUAL 0)
        message(FATAL_ERROR "Could not locate the Clang resource directory.")
    endif()
endif()
set(SLOPPY_CLANG_FUZZER_LIB "")
if(SLOPPY_ENABLE_LIBFUZZER AND MSVC)
    set(SLOPPY_CLANG_FUZZER_LIB
        "${SLOPPY_CLANG_RESOURCE_DIR}/lib/windows/clang_rt.fuzzer-x86_64.lib")
    if(NOT EXISTS "${SLOPPY_CLANG_FUZZER_LIB}")
        message(FATAL_ERROR "Clang libFuzzer runtime was not found: ${SLOPPY_CLANG_FUZZER_LIB}")
    endif()
endif()
set(SLOPPY_SIMD_REQUESTED OFF)
set(SLOPPY_SIMD_ENABLED OFF)
set(SLOPPY_BYTES_SIMD_SSE2_AVAILABLE OFF)
set(SLOPPY_BYTES_SIMD_AVX2_AVAILABLE OFF)
if(NOT SLOPPY_ENABLE_SIMD STREQUAL "AUTO"
   AND NOT SLOPPY_ENABLE_SIMD STREQUAL "ON"
   AND NOT SLOPPY_ENABLE_SIMD STREQUAL "OFF")
    message(FATAL_ERROR "SLOPPY_ENABLE_SIMD must be AUTO, ON, or OFF.")
endif()
if(NOT SLOPPY_SIMD_LEVEL STREQUAL "AUTO"
   AND NOT SLOPPY_SIMD_LEVEL STREQUAL "SSE2"
   AND NOT SLOPPY_SIMD_LEVEL STREQUAL "AVX2")
    message(FATAL_ERROR "SLOPPY_SIMD_LEVEL must be AUTO, SSE2, or AVX2.")
endif()
if(SLOPPY_ENABLE_SIMD STREQUAL "ON")
    set(SLOPPY_SIMD_REQUESTED ON)
endif()
if(NOT SLOPPY_ENABLE_SIMD STREQUAL "OFF")
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|X86_64|AMD64|amd64)$")
        if(SLOPPY_SIMD_LEVEL STREQUAL "AVX2")
            set(SLOPPY_BYTES_SIMD_AVX2_AVAILABLE ON)
            set(SLOPPY_SIMD_ENABLED ON)
        else()
            set(SLOPPY_BYTES_SIMD_SSE2_AVAILABLE ON)
            set(SLOPPY_SIMD_ENABLED ON)
        endif()
    elseif(SLOPPY_SIMD_REQUESTED)
        message(
            FATAL_ERROR
                "SLOPPY_ENABLE_SIMD=ON was requested, but no supported SIMD backend is available for ${CMAKE_SYSTEM_PROCESSOR}.")
    else()
        message(
            STATUS
                "No supported SIMD backend is available for ${CMAKE_SYSTEM_PROCESSOR}; using scalar primitives.")
    endif()
endif()
if(SLOPPY_BYTES_SIMD_AVX2_AVAILABLE)
    message(STATUS "SIMD byte/string backend: AVX2")
elseif(SLOPPY_BYTES_SIMD_SSE2_AVAILABLE)
    message(STATUS "SIMD byte/string backend: SSE2")
else()
    message(STATUS "SIMD byte/string backend: scalar")
endif()
set(SLOPPY_CLANG_ASAN_DYNAMIC_LIB "")
set(SLOPPY_CLANG_ASAN_THUNK_LIB "")
set(SLOPPY_CLANG_ASAN_DLL "")
if(SLOPPY_ENABLE_ASAN AND MSVC)
    set(SLOPPY_CLANG_ASAN_DYNAMIC_LIB
        "${SLOPPY_CLANG_RESOURCE_DIR}/lib/windows/clang_rt.asan_dynamic-x86_64.lib")
    set(SLOPPY_CLANG_ASAN_THUNK_LIB
        "${SLOPPY_CLANG_RESOURCE_DIR}/lib/windows/clang_rt.asan_dynamic_runtime_thunk-x86_64.lib")
    set(SLOPPY_CLANG_ASAN_DLL
        "${SLOPPY_CLANG_RESOURCE_DIR}/lib/windows/clang_rt.asan_dynamic-x86_64.dll")
    if(NOT EXISTS "${SLOPPY_CLANG_ASAN_DYNAMIC_LIB}")
        message(FATAL_ERROR "Clang ASan runtime was not found: ${SLOPPY_CLANG_ASAN_DYNAMIC_LIB}")
    endif()
    if(NOT EXISTS "${SLOPPY_CLANG_ASAN_THUNK_LIB}")
        message(FATAL_ERROR "Clang ASan thunk runtime was not found: ${SLOPPY_CLANG_ASAN_THUNK_LIB}")
    endif()
    if(NOT EXISTS "${SLOPPY_CLANG_ASAN_DLL}")
        message(FATAL_ERROR "Clang ASan DLL was not found: ${SLOPPY_CLANG_ASAN_DLL}")
    endif()
endif()
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
    ffi.js
    fs.js
    crypto.js
    net.js
    os.js
    problem-details.js
    request-id.js
    request-logging.js
    time.js
    workers.js
    index.js
    node/assert.js
    node/buffer.js
    node/crypto.js
    node/events.js
    node/fs.js
    node/fs/promises.js
    node/os.js
    node/path.js
    node/process.js
    node/querystring.js
    node/stream.js
    node/timers.js
    node/url.js
    node/util.js
    providers/sqlite.js
    results.js
    schema.js
    testing.js
    internal/capabilities.js
    internal/config.js
    internal/intrinsics.js
    internal/logging.js
    internal/modules.js
    internal/routes.js
    internal/services.js
    internal/shared.js
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
                "Rebuild/package V8 with the platform SDK builder.")
    endif()
endfunction()

function(sloppy_require_v8_manifest_true description)
    string(JSON actual_value GET "${SLOPPY_V8_MANIFEST}" ${ARGN})
    if(NOT actual_value)
        message(
            FATAL_ERROR
                "V8 bridge: SDK manifest ${description} must be true.\n"
                "Rebuild/package V8 with the platform SDK builder.")
    endif()
endfunction()

function(sloppy_get_v8_manifest_bool out_variable description)
    string(JSON actual_value GET "${SLOPPY_V8_MANIFEST}" ${ARGN})
    if(actual_value STREQUAL "true" OR actual_value STREQUAL "ON")
        set(${out_variable}
            TRUE
            PARENT_SCOPE)
    elseif(actual_value STREQUAL "false" OR actual_value STREQUAL "OFF")
        set(${out_variable}
            FALSE
            PARENT_SCOPE)
    else()
        message(
            FATAL_ERROR
                "V8 bridge: SDK manifest ${description} must be a boolean, got '${actual_value}'.")
    endif()
endfunction()

if(CMAKE_SYSTEM_NAME STREQUAL "Windows" AND CMAKE_SYSTEM_PROCESSOR MATCHES "AMD64|x86_64")
    set(SLOPPY_V8_EXPECTED_PLATFORM "windows-x64")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_SYSTEM_PROCESSOR MATCHES "AMD64|x86_64")
    set(SLOPPY_V8_EXPECTED_PLATFORM "linux-x64")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin" AND CMAKE_SYSTEM_PROCESSOR MATCHES "arm64|aarch64")
    set(SLOPPY_V8_EXPECTED_PLATFORM "macos-arm64")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin" AND CMAKE_SYSTEM_PROCESSOR MATCHES "AMD64|x86_64")
    set(SLOPPY_V8_EXPECTED_PLATFORM "macos-x64")
else()
    set(SLOPPY_V8_EXPECTED_PLATFORM "${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}")
endif()

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
    sloppy_require_v8_manifest_string("${SLOPPY_V8_EXPECTED_PLATFORM}" "platform" platform)
    sloppy_require_v8_manifest_string("${SLOPPY_V8_EXPECTED_REVISION}" "v8Revision" v8Revision)
    sloppy_require_v8_manifest_string("release" "buildType" buildType)
    if(SLOPPY_V8_EXPECTED_PLATFORM STREQUAL "windows-x64")
        sloppy_require_v8_manifest_string(
            "Release or RelWithDebInfo" "crtCompatibility" crtCompatibility)
        sloppy_require_v8_manifest_string(
            "${SLOPPY_V8_EXPECTED_CR_LIBCXX_REVISION}" "abi.crLibcxxRevision" abi
            crLibcxxRevision)
    elseif(SLOPPY_V8_EXPECTED_PLATFORM STREQUAL "linux-x64")
        sloppy_require_v8_manifest_string("sloppy-built-v8" "source" source)
        sloppy_require_v8_manifest_string("glibc clang-libc++ static-v8" "crtCompatibility"
                                          crtCompatibility)
        sloppy_require_v8_manifest_string(
            "${SLOPPY_V8_EXPECTED_CR_LIBCXX_REVISION}" "abi.crLibcxxRevision" abi
            crLibcxxRevision)
    endif()
    sloppy_require_v8_manifest_string("x64" "abi.v8TargetArch" abi v8TargetArch)
    sloppy_get_v8_manifest_bool(
        SLOPPY_V8_ABI_COMPRESS_POINTERS "abi.v8CompressPointers" abi v8CompressPointers)
    sloppy_get_v8_manifest_bool(
        SLOPPY_V8_ABI_COMPRESS_POINTERS_IN_SHARED_CAGE
        "abi.v8CompressPointersInSharedCage" abi v8CompressPointersInSharedCage)
    sloppy_get_v8_manifest_bool(
        SLOPPY_V8_ABI_31BIT_SMIS_ON_64BIT_ARCH
        "abi.v8_31BitSmisOn64BitArch" abi v8_31BitSmisOn64BitArch)
    sloppy_get_v8_manifest_bool(SLOPPY_V8_ABI_ENABLE_SANDBOX "abi.v8EnableSandbox" abi
                                v8EnableSandbox)
    if(SLOPPY_V8_EXPECTED_PLATFORM STREQUAL "windows-x64")
        if(NOT SLOPPY_V8_ABI_COMPRESS_POINTERS
           OR NOT SLOPPY_V8_ABI_COMPRESS_POINTERS_IN_SHARED_CAGE
           OR NOT SLOPPY_V8_ABI_ENABLE_SANDBOX)
            message(FATAL_ERROR "V8 bridge: SDK ABI flags must match the pinned SDK.")
        endif()
    elseif(SLOPPY_V8_EXPECTED_PLATFORM STREQUAL "linux-x64")
        if(NOT SLOPPY_V8_ABI_COMPRESS_POINTERS
           OR NOT SLOPPY_V8_ABI_COMPRESS_POINTERS_IN_SHARED_CAGE
           OR NOT SLOPPY_V8_ABI_ENABLE_SANDBOX)
            message(
                FATAL_ERROR
                    "V8 bridge: Linux SDK ABI flags must match the pinned clang-libc++ sandbox-enabled SDK.")
        endif()
    endif()

    file(
        GLOB
        SLOPPY_V8_MONOLITH_LIBS
        CONFIGURE_DEPENDS
        "${SLOPPY_V8_ROOT}/lib/v8_monolith*.lib"
        "${SLOPPY_V8_ROOT}/lib/libv8_monolith*.a"
        "${SLOPPY_V8_ROOT}/lib/libv8_monolith*.so")
    file(
        GLOB
        SLOPPY_V8_SPLIT_CORE_LIBS
        CONFIGURE_DEPENDS
        "${SLOPPY_V8_ROOT}/lib/v8.lib"
        "${SLOPPY_V8_ROOT}/lib/libv8.a"
        "${SLOPPY_V8_ROOT}/lib/libv8.so")
    file(
        GLOB
        SLOPPY_V8_PLATFORM_LIBS
        CONFIGURE_DEPENDS
        "${SLOPPY_V8_ROOT}/lib/v8_libplatform*.lib"
        "${SLOPPY_V8_ROOT}/lib/libv8_libplatform*.a"
        "${SLOPPY_V8_ROOT}/lib/libv8_libplatform*.so")
    file(
        GLOB
        SLOPPY_V8_BASE_LIBS
        CONFIGURE_DEPENDS
        "${SLOPPY_V8_ROOT}/lib/v8_libbase*.lib"
        "${SLOPPY_V8_ROOT}/lib/libv8_libbase*.a"
        "${SLOPPY_V8_ROOT}/lib/libv8_libbase*.so")
    file(
        GLOB
        SLOPPY_V8_SAMPLER_LIBS
        CONFIGURE_DEPENDS
        "${SLOPPY_V8_ROOT}/lib/libv8_libsampler*.a"
        "${SLOPPY_V8_ROOT}/lib/libv8_libsampler*.so")
    file(
        GLOB
        SLOPPY_V8_LIBCXX_LIBS
        CONFIGURE_DEPENDS
        "${SLOPPY_V8_ROOT}/lib/libc++.lib"
        "${SLOPPY_V8_ROOT}/lib/libc++.a"
        "${SLOPPY_V8_ROOT}/lib/libc++abi*.a"
        "${SLOPPY_V8_ROOT}/lib/libunwind*.a")

    if(SLOPPY_V8_MONOLITH_LIBS)
        if(NOT SLOPPY_V8_PLATFORM_LIBS)
            message(
                FATAL_ERROR
                    "V8 bridge: enabled but no v8_libplatform library was found under ${SLOPPY_V8_ROOT}/lib.")
        endif()
        if(NOT SLOPPY_V8_BASE_LIBS)
            message(
                FATAL_ERROR
                    "V8 bridge: enabled but no v8_libbase library was found under ${SLOPPY_V8_ROOT}/lib.")
        endif()
        set(SLOPPY_V8_LIBS ${SLOPPY_V8_MONOLITH_LIBS} ${SLOPPY_V8_PLATFORM_LIBS}
                           ${SLOPPY_V8_BASE_LIBS} ${SLOPPY_V8_SAMPLER_LIBS}
                           ${SLOPPY_V8_LIBCXX_LIBS})
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
                    "V8 bridge: enabled but no v8_libplatform library was found under ${SLOPPY_V8_ROOT}/lib.")
        endif()
        if(NOT SLOPPY_V8_BASE_LIBS)
            message(
                FATAL_ERROR
                    "V8 bridge: enabled but no v8_libbase library was found under ${SLOPPY_V8_ROOT}/lib.")
        endif()
        set(SLOPPY_V8_LIBS ${SLOPPY_V8_SPLIT_CORE_LIBS} ${SLOPPY_V8_PLATFORM_LIBS}
                           ${SLOPPY_V8_BASE_LIBS} ${SLOPPY_V8_SAMPLER_LIBS})
        message(STATUS "V8 SDK library mode: split")
    else()
        message(
            FATAL_ERROR
                "V8 bridge: enabled but no core V8 library was found under ${SLOPPY_V8_ROOT}/lib.\n"
                "Expected a monolithic V8 library, or split core V8 with v8_libplatform and v8_libbase libraries.")
    endif()

    add_library(Sloppy::V8 INTERFACE IMPORTED)
    target_include_directories(Sloppy::V8 INTERFACE "${SLOPPY_V8_ROOT}/include")
    target_compile_definitions(
        Sloppy::V8
        INTERFACE
            $<$<COMPILE_LANGUAGE:CXX>:V8_TARGET_ARCH_X64>)
    if(SLOPPY_V8_ABI_COMPRESS_POINTERS)
        target_compile_definitions(Sloppy::V8 INTERFACE $<$<COMPILE_LANGUAGE:CXX>:V8_COMPRESS_POINTERS>)
    endif()
    if(SLOPPY_V8_ABI_COMPRESS_POINTERS_IN_SHARED_CAGE)
        target_compile_definitions(
            Sloppy::V8 INTERFACE $<$<COMPILE_LANGUAGE:CXX>:V8_COMPRESS_POINTERS_IN_SHARED_CAGE>)
    endif()
    if(SLOPPY_V8_ABI_31BIT_SMIS_ON_64BIT_ARCH)
        target_compile_definitions(
            Sloppy::V8 INTERFACE $<$<COMPILE_LANGUAGE:CXX>:V8_31BIT_SMIS_ON_64BIT_ARCH>)
    endif()
    if(SLOPPY_V8_ABI_ENABLE_SANDBOX)
        target_compile_definitions(Sloppy::V8 INTERFACE $<$<COMPILE_LANGUAGE:CXX>:V8_ENABLE_SANDBOX>)
    endif()
    if(SLOPPY_V8_LIBCXX_LIBS)
        if(MSVC)
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
        elseif(UNIX AND NOT APPLE)
            target_compile_definitions(
                Sloppy::V8
                INTERFACE
                    $<$<COMPILE_LANGUAGE:CXX>:_LIBCPP_HARDENING_MODE=_LIBCPP_HARDENING_MODE_EXTENSIVE>
                    $<$<COMPILE_LANGUAGE:CXX>:CR_LIBCXX_REVISION=${SLOPPY_V8_EXPECTED_CR_LIBCXX_REVISION}>)
            target_compile_options(
                Sloppy::V8
                INTERFACE
                    $<$<COMPILE_LANGUAGE:CXX>:-nostdinc++>
                    $<$<COMPILE_LANGUAGE:CXX>:-isystem${SLOPPY_V8_ROOT}/support/libcxx/buildtools>
                    $<$<COMPILE_LANGUAGE:CXX>:-isystem${SLOPPY_V8_ROOT}/support/libcxx/include>)
            target_link_options(Sloppy::V8 INTERFACE $<$<LINK_LANGUAGE:CXX>:-nostdlib++>)
        endif()
    endif()
    target_link_libraries(Sloppy::V8 INTERFACE ${SLOPPY_V8_LIBS})
    if(UNIX AND NOT APPLE)
        target_link_libraries(Sloppy::V8 INTERFACE pthread dl atomic)
        target_link_options(Sloppy::V8 INTERFACE $<$<LINK_LANGUAGE:CXX>:-fuse-ld=lld>)
    endif()
    if(WIN32 AND SLOPPY_V8_MONOLITH_LIBS)
        # Chromium's monolithic Windows V8 library carries allocator-shim overrides for
        # CRT allocation symbols. lld-link requires the override policy to be explicit.
        target_link_options(Sloppy::V8 INTERFACE "/FORCE:MULTIPLE")
    endif()
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
