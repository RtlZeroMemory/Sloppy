if(NOT DEFINED PROJECT_SOURCE_DIR)
    message(FATAL_ERROR "PROJECT_SOURCE_DIR is required")
endif()
if(NOT DEFINED SLOPPY_CLI)
    message(FATAL_ERROR "SLOPPY_CLI is required")
endif()
if(NOT DEFINED SLOPPY_ARTIFACTS)
    message(FATAL_ERROR "SLOPPY_ARTIFACTS is required")
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

execute_process(
    COMMAND "${SLOPPY_CLI}" run --artifacts "${PROJECT_SOURCE_DIR}/${SLOPPY_ARTIFACTS}" --once
            "${SLOPPY_ONCE_METHOD}" "${SLOPPY_ONCE_TARGET}"
    WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
    RESULT_VARIABLE run_result
    OUTPUT_VARIABLE run_stdout
    ERROR_VARIABLE run_stderr)
if(NOT run_result EQUAL 0)
    message(
        FATAL_ERROR
            "Conformance artifact run failed for ${SLOPPY_ARTIFACTS}\nstdout:\n${run_stdout}\nstderr:\n${run_stderr}")
endif()

string(FIND "${run_stdout}" "${SLOPPY_EXPECTED_OUTPUT}" output_index)
if(output_index EQUAL -1)
    message(
        FATAL_ERROR
            "Conformance artifact output did not contain '${SLOPPY_EXPECTED_OUTPUT}'\nstdout:\n${run_stdout}\nstderr:\n${run_stderr}")
endif()
