if(NOT DEFINED SLOPPY_CLI)
    message(FATAL_ERROR "SLOPPY_CLI is required")
endif()

if(NOT DEFINED SLOPPY_EXPECTED)
    message(FATAL_ERROR "SLOPPY_EXPECTED is required")
endif()

execute_process(
    COMMAND "${SLOPPY_CLI}" ${SLOPPY_CLI_ARGS}
    WORKING_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}/../.."
    RESULT_VARIABLE result
    OUTPUT_VARIABLE stdout
    ERROR_VARIABLE stderr)

if(NOT result EQUAL 0)
    message(FATAL_ERROR "CLI command failed with ${result}\nstdout:\n${stdout}\nstderr:\n${stderr}")
endif()

file(READ "${SLOPPY_EXPECTED}" expected)

if(NOT stdout STREQUAL expected)
    message(FATAL_ERROR "CLI output mismatch for ${SLOPPY_EXPECTED}\nExpected:\n${expected}\nActual:\n${stdout}")
endif()
