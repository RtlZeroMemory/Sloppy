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

if(NOT output MATCHES "Ada" OR NOT output MATCHES "Grace" OR NOT output MATCHES "postgresTimedOut.*true" OR NOT output MATCHES "materializedRejected.*true" OR NOT output MATCHES "cursorCount.*152" OR NOT output MATCHES "cursorMaxRowsRejected.*true" OR NOT output MATCHES "poolPinned.*true" OR NOT output MATCHES "cursorTimedOut.*true" OR NOT output MATCHES "cacheEvidence.*getOk.*true" OR NOT output MATCHES "cacheEvidence.*ttlExpired.*true" OR NOT output MATCHES "cacheEvidence.*slidingExpired.*true" OR NOT output MATCHES "cacheEvidence.*tagInvalidated.*true" OR NOT output MATCHES "cacheEvidence.*cleanupOk.*true" OR NOT output MATCHES "cacheEvidence.*namespaceIsolation.*true" OR NOT output MATCHES "cacheEvidence.*hybridMemory.*true" OR NOT output MATCHES "cacheEvidence.*hybridGets.*true")
    message(FATAL_ERROR "PostgreSQL bridge live output did not include expected cursor, bounded materialization, pool pinning, and timeout evidence.\n${output}")
endif()

if(NOT output MATCHES "ormLane.*selectedEmail.*ada\\.orm@example\\.com"
   OR NOT output MATCHES "ormLane.*conflict.*true"
   OR NOT output MATCHES "ormLane.*oneInclude.*Core"
   OR NOT output MATCHES "ormLane.*manyIncludeCount.*1"
   OR NOT output MATCHES "ormLane.*rolledBack.*true"
   OR NOT output MATCHES "ormLane.*rawCount.*1"
   OR NOT output MATCHES "ormLane.*cursorCount.*132")
    message(FATAL_ERROR "PostgreSQL bridge live output did not include expected ORM migration, CRUD, include, rollback, and cursor evidence.\n${output}")
endif()
