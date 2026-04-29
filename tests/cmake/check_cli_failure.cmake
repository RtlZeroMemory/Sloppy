if(NOT DEFINED SLOPPY_CLI)
    message(FATAL_ERROR "SLOPPY_CLI is required")
endif()

execute_process(
    COMMAND "${SLOPPY_CLI}" ${SLOPPY_CLI_ARGS}
    WORKING_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}/../.."
    RESULT_VARIABLE result
    OUTPUT_VARIABLE stdout
    ERROR_VARIABLE stderr)

if(result EQUAL 0)
    message(FATAL_ERROR "CLI command unexpectedly succeeded\nstdout:\n${stdout}\nstderr:\n${stderr}")
endif()

if(NOT stderr MATCHES "metadata path not found|--plan")
    message(FATAL_ERROR "CLI failure did not explain the metadata problem\nstderr:\n${stderr}")
endif()
