cmake_minimum_required(VERSION 3.25)

project(Sloppy VERSION 0.0.0 LANGUAGES C)

include(cmake/SloppyOptions.cmake)
find_package(yyjson CONFIG REQUIRED)
find_package(llhttp CONFIG REQUIRED)
find_package(libuv CONFIG REQUIRED)
find_package(unofficial-libffi CONFIG REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(unofficial-sodium CONFIG REQUIRED)
find_package(xxHash CONFIG REQUIRED)
find_package(unofficial-sqlite3 CONFIG REQUIRED)
find_package(PostgreSQL REQUIRED)
find_package(ZLIB REQUIRED)
find_package(nghttp2 CONFIG QUIET)
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

if(TARGET unofficial::libffi::libffi)
    set(SLOPPY_LIBFFI_TARGET unofficial::libffi::libffi)
else()
    message(FATAL_ERROR "libffi dependency was found, but unofficial::libffi::libffi is not available.")
endif()

if(TARGET nghttp2::nghttp2)
    set(SLOPPY_NGHTTP2_TARGET nghttp2::nghttp2)
elseif(TARGET nghttp2::nghttp2_static)
    set(SLOPPY_NGHTTP2_TARGET nghttp2::nghttp2_static)
else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(NGHTTP2 REQUIRED IMPORTED_TARGET libnghttp2)
    set(SLOPPY_NGHTTP2_TARGET PkgConfig::NGHTTP2)
endif()


if(TARGET xxHash::xxhash)
    set(SLOPPY_XXHASH_TARGET xxHash::xxhash)
elseif(TARGET xxhash)
    set(SLOPPY_XXHASH_TARGET xxhash)
else()
    message(FATAL_ERROR "xxHash dependency was found, but no known CMake target is available.")
endif()

include(cmake/SloppySources.cmake)
add_library(sloppy_ryu STATIC third_party/ryu/ryu/d2s.c third_party/ryu/ryu/f2s.c)
target_include_directories(sloppy_ryu PUBLIC "${PROJECT_SOURCE_DIR}/third_party/ryu")
target_compile_features(sloppy_ryu PUBLIC c_std_17)

add_library(sloppy_core STATIC ${SLOPPY_CORE_SOURCES})
target_include_directories(sloppy_core PUBLIC "${PROJECT_SOURCE_DIR}/include")
target_compile_features(sloppy_core PUBLIC c_std_17)
target_link_libraries(
    sloppy_core
    PRIVATE sloppy_ryu
            yyjson::yyjson
            ${SLOPPY_LLHTTP_TARGET}
            ${SLOPPY_LIBUV_TARGET}
            ${SLOPPY_NGHTTP2_TARGET}
            ${SLOPPY_XXHASH_TARGET}
            OpenSSL::Crypto
            OpenSSL::SSL
            unofficial-sodium::sodium
            PostgreSQL::PostgreSQL
            unofficial::sqlite3::sqlite3
            ZLIB::ZLIB)
target_link_libraries(sloppy_core PUBLIC ${SLOPPY_LIBFFI_TARGET})
target_compile_definitions(sloppy_core PUBLIC SLOPPY_ENABLE_FFI=1)

if(CMAKE_DL_LIBS)
    target_link_libraries(sloppy_core PRIVATE ${CMAKE_DL_LIBS})
endif()

if(UNIX AND NOT APPLE)
    # vcpkg's static libpq may leave libpgcommon's math symbols to the consumer.
    # Keep libm after the PostgreSQL archive for GNU ld's left-to-right resolution.
    target_link_libraries(sloppy_core PRIVATE m)
endif()

if(WIN32)
    target_link_libraries(sloppy_core PRIVATE bcrypt)
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_libraries(sloppy_core PRIVATE m)
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
    target_link_libraries(sloppy_core PUBLIC Sloppy::V8)
endif()

if(SLOPPY_ENABLE_ASSERTS)
    target_compile_definitions(sloppy_core PUBLIC SL_ENABLE_ASSERTS=1)
else()
    target_compile_definitions(sloppy_core PUBLIC SL_ENABLE_ASSERTS=0)
endif()

if(SLOPPY_SIMD_ENABLED)
    target_compile_definitions(sloppy_core PUBLIC SL_ENABLE_SIMD=1)
else()
    target_compile_definitions(sloppy_core PUBLIC SL_ENABLE_SIMD=0)
endif()

if(SLOPPY_BYTES_SIMD_SSE2_AVAILABLE)
    target_compile_definitions(sloppy_core PRIVATE SL_BYTES_SIMD_SSE2=1 SL_STRING_SIMD_SSE2=1)
else()
    target_compile_definitions(sloppy_core PRIVATE SL_BYTES_SIMD_SSE2=0 SL_STRING_SIMD_SSE2=0)
endif()

if(SLOPPY_BYTES_SIMD_AVX2_AVAILABLE)
    target_compile_definitions(sloppy_core PRIVATE SL_BYTES_SIMD_AVX2=1 SL_STRING_SIMD_AVX2=1)
    if(MSVC)
        set_source_files_properties(
            src/core/bytes_simd_avx2.c src/core/string_simd_avx2.c
            PROPERTIES COMPILE_OPTIONS "/arch:AVX2")
    else()
        set_source_files_properties(
            src/core/bytes_simd_avx2.c src/core/string_simd_avx2.c
            PROPERTIES COMPILE_OPTIONS "-mavx2")
    endif()
else()
    target_compile_definitions(sloppy_core PRIVATE SL_BYTES_SIMD_AVX2=0 SL_STRING_SIMD_AVX2=0)
endif()

include(cmake/SloppyCli.cmake)
if(SLOPPY_ENABLE_V8 AND UNIX AND NOT APPLE)
    set_target_properties(sloppy PROPERTIES BUILD_RPATH "$ORIGIN;$ORIGIN/../engines/v8"
                                         INSTALL_RPATH "$ORIGIN;$ORIGIN/../engines/v8")
endif()
include(cmake/SloppyFrameworkExamples.cmake)

add_executable(
    sloppy_bench
    benchmarks/bench_main.c
    benchmarks/bench_v8_bridge.c
    benchmarks/bench_route_matcher.c
    benchmarks/bench_handler_dispatch.c
    benchmarks/bench_logging.c
    benchmarks/bench_memory.c)
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

include(cmake/SloppyQuality.cmake)
include(cmake/SloppyTests.cmake)
if(SLOPPY_BUILD_COMPILER)
    if(CARGO_EXECUTABLE)
        add_custom_target(
            sloppyc
            ALL
            COMMAND "${CARGO_EXECUTABLE}" build --manifest-path
                    "${PROJECT_SOURCE_DIR}/compiler/Cargo.toml" ${SLOPPYC_CARGO_BUILD_PROFILE_ARGS}
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
    install(PROGRAMS "${SLOPPYC_BUILT_EXECUTABLE}" DESTINATION bin)
endif()
