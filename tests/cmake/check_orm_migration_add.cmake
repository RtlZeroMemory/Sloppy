if(NOT DEFINED SLOPPY_CLI)
    message(FATAL_ERROR "SLOPPY_CLI is required")
endif()
if(NOT DEFINED SLOPPY_SOURCE_DIR)
    message(FATAL_ERROR "SLOPPY_SOURCE_DIR is required")
endif()
if(NOT DEFINED SLOPPY_EXPECTED)
    message(FATAL_ERROR "SLOPPY_EXPECTED is required")
endif()

set(work_dir "${CMAKE_CURRENT_BINARY_DIR}/orm-migration-add")
file(REMOVE_RECURSE "${work_dir}")
file(MAKE_DIRECTORY "${work_dir}")
file(COPY "${SLOPPY_SOURCE_DIR}/tests/fixtures/cli/orm-migration/" DESTINATION "${work_dir}")

execute_process(
    COMMAND "${SLOPPY_CLI}" orm migration add CreateUsers "${work_dir}/compiled" --provider main
    WORKING_DIRECTORY "${SLOPPY_SOURCE_DIR}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE stdout
    ERROR_VARIABLE stderr)

if(NOT result EQUAL 0)
    message(FATAL_ERROR "sloppy orm migration add failed\nstdout:\n${stdout}\nstderr:\n${stderr}")
endif()
if(NOT stdout MATCHES "created .*0001_create_users\\.sql")
    message(FATAL_ERROR "sloppy orm migration add did not report the created migration\nstdout:\n${stdout}")
endif()

set(created "${work_dir}/migrations/0001_create_users.sql")
if(NOT EXISTS "${created}")
    message(FATAL_ERROR "sloppy orm migration add did not create ${created}")
endif()

file(READ "${created}" actual)
file(READ "${SLOPPY_EXPECTED}" expected)
string(REPLACE "\r\n" "\n" actual "${actual}")
string(REPLACE "\r\n" "\n" expected "${expected}")
if(NOT actual STREQUAL expected)
    message(FATAL_ERROR "created ORM migration did not match expected SQL\nactual:\n${actual}\nexpected:\n${expected}")
endif()
