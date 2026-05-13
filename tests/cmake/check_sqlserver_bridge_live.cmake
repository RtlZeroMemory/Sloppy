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

if(NOT output MATCHES "Ada" OR NOT output MATCHES "Grace" OR NOT output MATCHES "sqlserverTimedOut.*true" OR NOT output MATCHES "materializedRejected.*true" OR NOT output MATCHES "cursorCount.*152" OR NOT output MATCHES "cursorMaxRowsRejected.*true" OR NOT output MATCHES "poolPinned.*true" OR NOT output MATCHES "cursorTimedOut.*true")
    message(FATAL_ERROR "SQL Server bridge live output did not include expected cursor, bounded materialization, pool pinning, and timeout evidence.\n${output}")
endif()

if(NOT output MATCHES "ormLane" OR NOT output MATCHES "selectedEmail.*ada\\.orm@example\\.com" OR NOT output MATCHES "conflict.*true" OR NOT output MATCHES "oneInclude.*Core" OR NOT output MATCHES "manyIncludeCount.*1" OR NOT output MATCHES "rolledBack.*true" OR NOT output MATCHES "cursorCount.*132")
    message(FATAL_ERROR "SQL Server bridge live output did not include expected ORM migration, CRUD, include, rollback, and cursor evidence.\n${output}")
endif()
