if(NOT DEFINED SLOPPY_CLI)
    message(FATAL_ERROR "SLOPPY_CLI is required")
endif()

if(NOT DEFINED SLOPPY_EXPECTED_STDERR)
    message(FATAL_ERROR "SLOPPY_EXPECTED_STDERR is required")
endif()

if(DEFINED SLOPPY_ENV_NAME)
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E env "${SLOPPY_ENV_NAME}=${SLOPPY_ENV_VALUE}"
                "${SLOPPY_CLI}" ${SLOPPY_CLI_ARGS}
        WORKING_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}/../.."
        RESULT_VARIABLE result
        OUTPUT_VARIABLE stdout
        ERROR_VARIABLE stderr)
else()
    execute_process(
        COMMAND "${SLOPPY_CLI}" ${SLOPPY_CLI_ARGS}
        WORKING_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}/../.."
        RESULT_VARIABLE result
        OUTPUT_VARIABLE stdout
        ERROR_VARIABLE stderr)
endif()

if(result EQUAL 0)
    message(FATAL_ERROR "CLI command unexpectedly succeeded\nstdout:\n${stdout}\nstderr:\n${stderr}")
endif()

if(NOT stderr MATCHES "${SLOPPY_EXPECTED_STDERR}")
    message(FATAL_ERROR "CLI stderr did not match expected pattern\nExpected:\n${SLOPPY_EXPECTED_STDERR}\nActual:\n${stderr}")
endif()

if(DEFINED SLOPPY_FORBIDDEN_STDERR AND stderr MATCHES "${SLOPPY_FORBIDDEN_STDERR}")
    message(FATAL_ERROR "CLI stderr leaked forbidden text\nForbidden:\n${SLOPPY_FORBIDDEN_STDERR}\nActual:\n${stderr}")
endif()
