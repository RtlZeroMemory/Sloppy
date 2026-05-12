if(NOT DEFINED PROJECT_SOURCE_DIR)
    message(FATAL_ERROR "PROJECT_SOURCE_DIR is required")
endif()

if(NOT DEFINED CMAKE_BINARY_DIR)
    message(FATAL_ERROR "CMAKE_BINARY_DIR is required")
endif()

if(NOT DEFINED SLOPPY_CLI)
    message(FATAL_ERROR "SLOPPY_CLI is required")
endif()

set(source_dir "${PROJECT_SOURCE_DIR}/compiler/tests/fixtures/grouped-route/expected")
set(work_dir "${CMAKE_BINARY_DIR}/tampered-route-artifact")

file(REMOVE_RECURSE "${work_dir}")
file(MAKE_DIRECTORY "${work_dir}")
file(COPY "${source_dir}/app.plan.json" "${source_dir}/app.js" "${source_dir}/app.js.map"
          "${source_dir}/routes.slrt" DESTINATION "${work_dir}")
file(WRITE "${work_dir}/routes.slrt" "stale route dispatch artifact")

execute_process(
    COMMAND "${SLOPPY_CLI}" run --artifacts "${work_dir}" --once GET /users/42
    WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE stdout
    ERROR_VARIABLE stderr)

if(result EQUAL 0)
    message(FATAL_ERROR
            "tampered routes.slrt was accepted before serving\nstdout:\n${stdout}\nstderr:\n${stderr}")
endif()

if(NOT stderr MATCHES "invalid route dispatch artifact")
    message(FATAL_ERROR
            "tampered routes.slrt failure did not report artifact validation\nstdout:\n${stdout}\nstderr:\n${stderr}")
endif()
