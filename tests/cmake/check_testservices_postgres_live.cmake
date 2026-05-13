if(NOT DEFINED ENV{SLOPPY_TESTSERVICES_POSTGRES_LIVE} OR "$ENV{SLOPPY_TESTSERVICES_POSTGRES_LIVE}" STREQUAL "")
    message("SKIP: live PostgreSQL TestServices lane is not configured; set SLOPPY_TESTSERVICES_POSTGRES_LIVE=1.")
    return()
endif()

execute_process(
    COMMAND "${SLOPPY_CLI}" run --artifacts tests/integration/execution/testservices_postgres_live
            --once GET /testservices/postgres
    WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE error
    TIMEOUT 600
)

if(NOT result EQUAL 0)
    message(FATAL_ERROR "PostgreSQL TestServices live run failed.\nstdout:\n${output}\nstderr:\n${error}")
endif()

if(NOT output MATCHES "\"ok\":true" OR NOT output MATCHES "\"provider\":\"postgres\"" OR NOT output MATCHES "\"cleanup\":\"removed\"")
    message(FATAL_ERROR "PostgreSQL TestServices live output did not include expected readiness, provider, reset, and cleanup evidence.\n${output}")
endif()
