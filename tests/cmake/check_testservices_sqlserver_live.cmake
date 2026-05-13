if(NOT DEFINED ENV{SLOPPY_TESTSERVICES_SQLSERVER_LIVE} OR "$ENV{SLOPPY_TESTSERVICES_SQLSERVER_LIVE}" STREQUAL "")
    message("SKIP: live SQL Server TestServices lane is not configured; set SLOPPY_TESTSERVICES_SQLSERVER_LIVE=1.")
    return()
endif()

execute_process(
    COMMAND "${SLOPPY_CLI}" run --artifacts tests/integration/execution/testservices_sqlserver_live
            --once GET /testservices/sqlserver
    WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE error
    TIMEOUT 600
)

if(NOT result EQUAL 0)
    message(FATAL_ERROR "SQL Server TestServices live run failed.\nstdout:\n${output}\nstderr:\n${error}")
endif()

if(NOT output MATCHES "\"ok\":true" OR NOT output MATCHES "\"provider\":\"sqlserver\"" OR NOT output MATCHES "\"cleanup\":\"removed\"")
    message(FATAL_ERROR "SQL Server TestServices live output did not include expected readiness, provider, reset, and cleanup evidence.\n${output}")
endif()
