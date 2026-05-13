if(NOT DEFINED SLOPPY_CLI)
    message(FATAL_ERROR "SLOPPY_CLI is required")
endif()

get_filename_component(repo_root "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
set(temp_root "$ENV{TEMP}")
if(temp_root STREQUAL "")
    set(temp_root "${repo_root}/build")
endif()
string(RANDOM LENGTH 12 ALPHABET "abcdefghijklmnopqrstuvwxyz0123456789" case_id)
set(output_path "${temp_root}/sloppy-openapi-preserve-${case_id}.json")
set(sentinel "already here")
file(WRITE "${output_path}" "${sentinel}")

execute_process(
    COMMAND
        "${SLOPPY_CLI}" openapi --plan tests/fixtures/cli/openapi-missing-schema.plan.json
        --strict --output "${output_path}"
    WORKING_DIRECTORY "${repo_root}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE stdout
    ERROR_VARIABLE stderr)

if(result EQUAL 0)
    file(REMOVE "${output_path}")
    message(FATAL_ERROR "openapi --strict unexpectedly succeeded\nstdout:\n${stdout}\nstderr:\n${stderr}")
endif()
if(NOT stderr MATCHES "--strict requires complete route contracts")
    file(REMOVE "${output_path}")
    message(FATAL_ERROR "openapi failure did not explain strict contract failure\nstderr:\n${stderr}")
endif()

file(READ "${output_path}" preserved)
file(REMOVE "${output_path}")
if(NOT "${preserved}" STREQUAL "${sentinel}")
    message(FATAL_ERROR "openapi --output rewrote output before validation failure")
endif()
