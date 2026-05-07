if(NOT DEFINED ENV{SLOPPY_POSTGRES_TEST_URL} OR "$ENV{SLOPPY_POSTGRES_TEST_URL}" STREQUAL "")
    message("SKIP: live PostgreSQL V8 bridge tests are not configured; set SLOPPY_POSTGRES_TEST_URL.")
    return()
endif()

execute_process(
    COMMAND "${SLOPPY_CLI}" run --artifacts tests/integration/execution/postgres_bridge --once GET /postgres
    WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE error
)

if(NOT result EQUAL 0)
    message(FATAL_ERROR "PostgreSQL bridge live run failed without printing secrets.\n${error}")
endif()

if(NOT output MATCHES "Ada" OR NOT output MATCHES "Grace")
    message(FATAL_ERROR "PostgreSQL bridge live output did not include expected rows.\n${output}")
endif()
