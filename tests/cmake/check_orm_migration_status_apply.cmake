if(NOT DEFINED SLOPPY_CLI)
    message(FATAL_ERROR "SLOPPY_CLI is required")
endif()
if(NOT DEFINED SLOPPY_SOURCE_DIR)
    message(FATAL_ERROR "SLOPPY_SOURCE_DIR is required")
endif()
if(NOT DEFINED SLOPPY_EXPECTED)
    message(FATAL_ERROR "SLOPPY_EXPECTED is required")
endif()

set(work_dir "${CMAKE_CURRENT_BINARY_DIR}/orm-migration-status-apply")
file(REMOVE_RECURSE "${work_dir}")
file(MAKE_DIRECTORY "${work_dir}")
file(COPY "${SLOPPY_SOURCE_DIR}/tests/fixtures/cli/orm-migration/" DESTINATION "${work_dir}")

execute_process(
    COMMAND "${SLOPPY_CLI}" orm migration add CreateUsers "${work_dir}/compiled" --provider main
    WORKING_DIRECTORY "${SLOPPY_SOURCE_DIR}"
    RESULT_VARIABLE add_result
    OUTPUT_VARIABLE add_stdout
    ERROR_VARIABLE add_stderr)
if(NOT add_result EQUAL 0)
    message(FATAL_ERROR "sloppy orm migration add failed\nstdout:\n${add_stdout}\nstderr:\n${add_stderr}")
endif()

execute_process(
    COMMAND "${SLOPPY_CLI}" orm migration status "${work_dir}/compiled" --provider main
    WORKING_DIRECTORY "${SLOPPY_SOURCE_DIR}"
    RESULT_VARIABLE pending_result
    OUTPUT_VARIABLE pending_stdout
    ERROR_VARIABLE pending_stderr)
if(NOT pending_result EQUAL 0 OR NOT pending_stdout MATCHES "\\[pending\\] 0001_create_users\\.sql")
    message(FATAL_ERROR "sloppy orm migration status did not report pending\nstdout:\n${pending_stdout}\nstderr:\n${pending_stderr}")
endif()

execute_process(
    COMMAND "${SLOPPY_CLI}" orm migration apply "${work_dir}/compiled" --provider main
    WORKING_DIRECTORY "${SLOPPY_SOURCE_DIR}"
    RESULT_VARIABLE apply_result
    OUTPUT_VARIABLE apply_stdout
    ERROR_VARIABLE apply_stderr)
if(NOT apply_result EQUAL 0 OR NOT apply_stdout MATCHES "\\[applied\\] 0001_create_users\\.sql")
    message(FATAL_ERROR "sloppy orm migration apply did not apply migration\nstdout:\n${apply_stdout}\nstderr:\n${apply_stderr}")
endif()

execute_process(
    COMMAND "${SLOPPY_CLI}" orm migration status "${work_dir}/compiled" --provider main --format json
    WORKING_DIRECTORY "${SLOPPY_SOURCE_DIR}"
    RESULT_VARIABLE json_result
    OUTPUT_VARIABLE json_stdout
    ERROR_VARIABLE json_stderr)
if(NOT json_result EQUAL 0 OR
   NOT json_stdout MATCHES "\"status\":\"current\"" OR
   NOT json_stdout MATCHES "\"status\":\"applied\"")
    message(FATAL_ERROR "sloppy orm migration status JSON did not report current/applied\nstdout:\n${json_stdout}\nstderr:\n${json_stderr}")
endif()

file(APPEND "${work_dir}/migrations/0001_create_users.sql" "\n-- tamper\n")
execute_process(
    COMMAND "${SLOPPY_CLI}" orm migration status "${work_dir}/compiled" --provider main
    WORKING_DIRECTORY "${SLOPPY_SOURCE_DIR}"
    RESULT_VARIABLE changed_result
    OUTPUT_VARIABLE changed_stdout
    ERROR_VARIABLE changed_stderr)
if(changed_result EQUAL 0 OR
   NOT changed_stdout MATCHES "\\[changed\\] 0001_create_users\\.sql" OR
   NOT changed_stderr MATCHES "applied migration hash changed")
    message(FATAL_ERROR "sloppy orm migration status did not fail on checksum mismatch\nstdout:\n${changed_stdout}\nstderr:\n${changed_stderr}")
endif()
