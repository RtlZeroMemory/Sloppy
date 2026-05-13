if(NOT DEFINED SLOPPY_CLI)
    message(FATAL_ERROR "SLOPPY_CLI is required")
endif()

set(case_root "${CMAKE_BINARY_DIR}/jobs-cli-test")
set(db_path "${case_root}/jobs.db")
file(REMOVE_RECURSE "${case_root}")
file(MAKE_DIRECTORY "${case_root}")

function(run_jobs_cli label)
    execute_process(
        COMMAND "${SLOPPY_CLI}" ${ARGN}
        WORKING_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}/../.."
        RESULT_VARIABLE result
        OUTPUT_VARIABLE stdout
        ERROR_VARIABLE stderr)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "${label} failed with ${result}\nstdout:\n${stdout}\nstderr:\n${stderr}")
    endif()
    set(run_jobs_cli_stdout "${stdout}" PARENT_SCOPE)
endfunction()

run_jobs_cli("jobs init" jobs init --provider sqlite --database "${db_path}")
if(NOT run_jobs_cli_stdout MATCHES "jobs schema initialized")
    message(FATAL_ERROR "jobs init did not report schema initialization")
endif()

run_jobs_cli("jobs status" jobs status --provider sqlite --database "${db_path}")
run_jobs_cli("jobs list" jobs list --provider sqlite --database "${db_path}")
run_jobs_cli("jobs workers" jobs workers --provider sqlite --database "${db_path}")
run_jobs_cli("jobs locks" jobs locks --provider sqlite --database "${db_path}")
run_jobs_cli("jobs recurring list" jobs recurring list --provider sqlite --database "${db_path}")

file(REMOVE_RECURSE "${case_root}")
