if(NOT DEFINED ENV{SLOPPY_SQLSERVER_TEST_CONNECTION_STRING} OR "$ENV{SLOPPY_SQLSERVER_TEST_CONNECTION_STRING}" STREQUAL "")
    message("SKIP: live SQL Server V8 bridge tests are not configured; set SLOPPY_SQLSERVER_TEST_CONNECTION_STRING.")
    return()
endif()

execute_process(
    COMMAND "${SLOPPY_CLI}" run --artifacts tests/integration/execution/sqlserver_bridge --once GET /sqlserver
    WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE error
)

if(NOT result EQUAL 0)
    message(FATAL_ERROR "SQL Server bridge live run failed without printing secrets.\n${error}")
endif()

if(output MATCHES "driver async support is unavailable")
    message("SKIP: live SQL Server V8 bridge true-async path is unavailable with the configured ODBC driver.")
    return()
endif()

if(NOT output MATCHES "Ada" OR NOT output MATCHES "Grace" OR NOT output MATCHES "sqlserverTimedOut.*true")
    message(FATAL_ERROR "SQL Server bridge live output did not include expected rows and timeout cancellation evidence.\n${output}")
endif()
