if(NOT DEFINED PROJECT_SOURCE_DIR)
    message(FATAL_ERROR "PROJECT_SOURCE_DIR is required")
endif()
if(NOT DEFINED CMAKE_BINARY_DIR)
    message(FATAL_ERROR "CMAKE_BINARY_DIR is required")
endif()
if(NOT DEFINED CARGO_EXECUTABLE)
    message(FATAL_ERROR "CARGO_EXECUTABLE is required")
endif()
if(NOT DEFINED SLOPPY_CLI)
    message(FATAL_ERROR "SLOPPY_CLI is required")
endif()
if(NOT DEFINED SLOPPY_CASE)
    message(FATAL_ERROR "SLOPPY_CASE is required")
endif()
if(NOT DEFINED SLOPPY_SOURCE)
    message(FATAL_ERROR "SLOPPY_SOURCE is required")
endif()

set(source_path "${PROJECT_SOURCE_DIR}/${SLOPPY_SOURCE}")
set(output_dir "${CMAKE_BINARY_DIR}/example-tooling/${SLOPPY_CASE}")
set(plan_path "${output_dir}/app.plan.json")

file(REMOVE_RECURSE "${output_dir}")
file(MAKE_DIRECTORY "${output_dir}")

execute_process(
    COMMAND "${CARGO_EXECUTABLE}" run --manifest-path
            "${PROJECT_SOURCE_DIR}/compiler/Cargo.toml" -- build "${source_path}" --out
            "${output_dir}"
    WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
    RESULT_VARIABLE build_result
    OUTPUT_VARIABLE build_stdout
    ERROR_VARIABLE build_stderr)
if(NOT build_result EQUAL 0)
    message(
        FATAL_ERROR
            "Example tooling build failed for ${SLOPPY_CASE}\nstdout:\n${build_stdout}\nstderr:\n${build_stderr}")
endif()

if(NOT EXISTS "${plan_path}")
    message(FATAL_ERROR "Example tooling build did not emit ${plan_path}")
endif()

function(run_tool command_name output_var)
    execute_process(
        COMMAND "${SLOPPY_CLI}" "${command_name}" --plan "${plan_path}" --format text
        WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
        RESULT_VARIABLE tool_result
        OUTPUT_VARIABLE tool_stdout
        ERROR_VARIABLE tool_stderr)
    if(NOT tool_result EQUAL 0)
        message(
            FATAL_ERROR
                "sloppy ${command_name} failed for ${SLOPPY_CASE}\nstdout:\n${tool_stdout}\nstderr:\n${tool_stderr}")
    endif()
    set(${output_var} "${tool_stdout}" PARENT_SCOPE)
endfunction()

run_tool(routes routes_output)
run_tool(doctor doctor_output)
run_tool(capabilities capabilities_output)

execute_process(
    COMMAND "${SLOPPY_CLI}" audit --plan "${plan_path}" --format json
    WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
    RESULT_VARIABLE audit_result
    OUTPUT_VARIABLE audit_output
    ERROR_VARIABLE audit_stderr)
if(NOT audit_result EQUAL 0)
    message(
        FATAL_ERROR
            "sloppy audit failed for ${SLOPPY_CASE}\nstdout:\n${audit_output}\nstderr:\n${audit_stderr}")
endif()

execute_process(
    COMMAND "${SLOPPY_CLI}" openapi --plan "${plan_path}"
    WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
    RESULT_VARIABLE openapi_result
    OUTPUT_VARIABLE openapi_output
    ERROR_VARIABLE openapi_stderr)
if(NOT openapi_result EQUAL 0)
    message(
        FATAL_ERROR
            "sloppy openapi failed for ${SLOPPY_CASE}\nstdout:\n${openapi_output}\nstderr:\n${openapi_stderr}")
endif()

function(require_match text pattern description)
    if(NOT "${text}" MATCHES "${pattern}")
        message(FATAL_ERROR "${description} did not match '${pattern}'\n${text}")
    endif()
endfunction()

if(DEFINED SLOPPY_EXPECTED_PLAN)
    file(READ "${plan_path}" plan_text)
    require_match("${plan_text}" "${SLOPPY_EXPECTED_PLAN}" "Plan output for ${SLOPPY_CASE}")
endif()
if(DEFINED SLOPPY_EXPECTED_ROUTES)
    require_match("${routes_output}" "${SLOPPY_EXPECTED_ROUTES}" "routes output for ${SLOPPY_CASE}")
endif()
if(DEFINED SLOPPY_EXPECTED_DOCTOR)
    require_match("${doctor_output}" "${SLOPPY_EXPECTED_DOCTOR}" "doctor output for ${SLOPPY_CASE}")
endif()
if(DEFINED SLOPPY_EXPECTED_CAPABILITIES)
    require_match(
        "${capabilities_output}" "${SLOPPY_EXPECTED_CAPABILITIES}"
        "capabilities output for ${SLOPPY_CASE}")
endif()
if(DEFINED SLOPPY_EXPECTED_OPENAPI)
    require_match("${openapi_output}" "${SLOPPY_EXPECTED_OPENAPI}" "OpenAPI output for ${SLOPPY_CASE}")
endif()
