if(NOT DEFINED PROJECT_SOURCE_DIR)
    message(FATAL_ERROR "PROJECT_SOURCE_DIR is required")
endif()
if(NOT DEFINED CMAKE_BINARY_DIR)
    message(FATAL_ERROR "CMAKE_BINARY_DIR is required")
endif()
if(NOT DEFINED CARGO_EXECUTABLE)
    message(FATAL_ERROR "CARGO_EXECUTABLE is required")
endif()
if(NOT DEFINED SLOPPY_CLI)
    message(FATAL_ERROR "SLOPPY_CLI is required")
endif()
if(NOT DEFINED SLOPPY_CONFORMANCE_CASE)
    message(FATAL_ERROR "SLOPPY_CONFORMANCE_CASE is required")
endif()
if(NOT DEFINED SLOPPY_SOURCE)
    message(FATAL_ERROR "SLOPPY_SOURCE is required")
endif()
if(NOT DEFINED SLOPPY_ONCE_METHOD)
    message(FATAL_ERROR "SLOPPY_ONCE_METHOD is required")
endif()
if(NOT DEFINED SLOPPY_ONCE_TARGET)
    message(FATAL_ERROR "SLOPPY_ONCE_TARGET is required")
endif()
if(NOT DEFINED SLOPPY_EXPECTED_OUTPUT)
    message(FATAL_ERROR "SLOPPY_EXPECTED_OUTPUT is required")
endif()

set(source_path "${PROJECT_SOURCE_DIR}/${SLOPPY_SOURCE}")
set(output_dir "${CMAKE_BINARY_DIR}/conformance/run-once/${SLOPPY_CONFORMANCE_CASE}/artifacts")
set(timeout_seconds 180)

file(REMOVE_RECURSE "${output_dir}")
file(MAKE_DIRECTORY "${output_dir}")

execute_process(
    COMMAND "${CARGO_EXECUTABLE}" run --manifest-path "${PROJECT_SOURCE_DIR}/compiler/Cargo.toml"
            -- build "${source_path}" --out "${output_dir}"
    WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
    TIMEOUT "${timeout_seconds}"
    RESULT_VARIABLE build_result
    OUTPUT_VARIABLE build_stdout
    ERROR_VARIABLE build_stderr)
if(NOT build_result EQUAL 0)
    if(build_result MATCHES "timeout")
        set(build_reason "timed out after ${timeout_seconds} seconds")
    else()
        set(build_reason "failed with result ${build_result}")
    endif()
    message(
        FATAL_ERROR
            "Conformance build ${build_reason} before run-once\nstdout:\n${build_stdout}\nstderr:\n${build_stderr}")
endif()

execute_process(
    COMMAND "${SLOPPY_CLI}" run --artifacts "${output_dir}" --once "${SLOPPY_ONCE_METHOD}"
            "${SLOPPY_ONCE_TARGET}"
    WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
    TIMEOUT "${timeout_seconds}"
    RESULT_VARIABLE run_result
    OUTPUT_VARIABLE run_stdout
    ERROR_VARIABLE run_stderr)
if(NOT run_result EQUAL 0)
    if(run_result MATCHES "timeout")
        set(run_reason "timed out after ${timeout_seconds} seconds")
    else()
        set(run_reason "failed with result ${run_result}")
    endif()
    message(
        FATAL_ERROR
            "Conformance run-once ${run_reason} for ${SLOPPY_CONFORMANCE_CASE}\nstdout:\n${run_stdout}\nstderr:\n${run_stderr}")
endif()

string(REGEX MATCH "${SLOPPY_EXPECTED_OUTPUT}" output_match "${run_stdout}")
if(NOT output_match)
    message(
        FATAL_ERROR
            "Conformance run-once output did not match '${SLOPPY_EXPECTED_OUTPUT}'\nstdout:\n${run_stdout}\nstderr:\n${run_stderr}")
endif()
