if(NOT DEFINED SLOPPY_CLI)
    message(FATAL_ERROR "SLOPPY_CLI is required")
endif()

if(SLOPPY_EXPECT_NO_STDLIB_FS)
    get_filename_component(repo_root "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
    list(FIND SLOPPY_CLI_ARGS "--artifacts" artifacts_flag_index)
    if(artifacts_flag_index EQUAL -1)
        message(FATAL_ERROR "stdlib.fs precondition test did not pass --artifacts")
    endif()
    math(EXPR artifacts_value_index "${artifacts_flag_index} + 1")
    list(LENGTH SLOPPY_CLI_ARGS cli_arg_count)
    if(NOT artifacts_value_index LESS cli_arg_count)
        message(FATAL_ERROR "stdlib.fs precondition test did not pass an artifact path")
    endif()
    list(GET SLOPPY_CLI_ARGS ${artifacts_value_index} artifacts_path_value)

    if(IS_ABSOLUTE "${artifacts_path_value}")
        set(artifact_dir "${artifacts_path_value}")
    else()
        set(artifact_dir "${repo_root}/${artifacts_path_value}")
    endif()

    set(artifact_plan_path "${artifact_dir}/app.plan.json")
    if(NOT EXISTS "${artifact_plan_path}")
        message(FATAL_ERROR
                "stdlib.fs precondition test could not inspect app.plan.json: ${artifact_plan_path}")
    endif()
    file(READ "${artifact_plan_path}" artifact_plan_json)
    if(artifact_plan_json MATCHES "stdlib[.]fs" OR artifact_plan_json MATCHES "sloppy/fs")
        message(FATAL_ERROR
                "runtime artifact unexpectedly declares or imports stdlib.fs: ${artifact_plan_path}")
    endif()

    set(artifact_bundle_path "${artifact_dir}/app.js")
    if(EXISTS "${artifact_bundle_path}")
        file(READ "${artifact_bundle_path}" artifact_bundle_js)
        if(artifact_bundle_js MATCHES "sloppy/fs")
            message(FATAL_ERROR
                    "runtime artifact unexpectedly imports sloppy/fs: ${artifact_bundle_path}")
        endif()
    endif()
endif()

execute_process(
    COMMAND "${SLOPPY_CLI}" ${SLOPPY_CLI_ARGS}
    WORKING_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}/../.."
    RESULT_VARIABLE result
    OUTPUT_VARIABLE stdout
    ERROR_VARIABLE stderr)

if(result EQUAL 0)
    message(FATAL_ERROR "CLI command unexpectedly succeeded\nstdout:\n${stdout}\nstderr:\n${stderr}")
endif()

if(DEFINED SLOPPY_EXPECTED_ERROR)
    if(NOT stderr MATCHES "${SLOPPY_EXPECTED_ERROR}")
        message(FATAL_ERROR "CLI failure did not explain the metadata problem\nstderr:\n${stderr}")
    endif()
else()
    if(NOT stderr MATCHES "metadata path not found")
        message(FATAL_ERROR "CLI failure did not report missing metadata\nstderr:\n${stderr}")
    endif()

    list(FIND SLOPPY_CLI_ARGS "--plan" plan_flag_index)
    if(plan_flag_index EQUAL -1)
        message(FATAL_ERROR "CLI failure test did not pass --plan\nstderr:\n${stderr}")
    endif()
    math(EXPR plan_value_index "${plan_flag_index} + 1")
    list(LENGTH SLOPPY_CLI_ARGS cli_arg_count)
    if(NOT plan_value_index LESS cli_arg_count)
        message(FATAL_ERROR "CLI failure test did not pass a --plan path\nstderr:\n${stderr}")
    endif()
    list(GET SLOPPY_CLI_ARGS ${plan_value_index} plan_path_value)
    string(FIND "${stderr}" "${plan_path_value}" plan_path_index)
    if(plan_path_index EQUAL -1)
        message(FATAL_ERROR "CLI failure did not include failing --plan path\nstderr:\n${stderr}")
    endif()
endif()
