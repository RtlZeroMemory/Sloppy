# sloppyc compiler conformance compile/reject tests. Included by cmake/SloppyTests.cmake.

    if(CARGO_EXECUTABLE AND SLOPPY_BUILD_COMPILER)
        function(sloppy_add_conformance_compile_test test_name case_name source_path)
            add_test(
                NAME ${test_name}
                COMMAND
                    "${CMAKE_COMMAND}" "-DPROJECT_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
                    "-DCMAKE_BINARY_DIR=${CMAKE_BINARY_DIR}"
                    "-DCARGO_EXECUTABLE=${CARGO_EXECUTABLE}"
                    "-DSLOPPY_CONFORMANCE_CASE=${case_name}" "-DSLOPPY_SOURCE=${source_path}"
                    -P "${PROJECT_SOURCE_DIR}/tests/cmake/check_conformance_compile_artifacts.cmake")
            set_tests_properties(${test_name} PROPERTIES LABELS "conformance")
        endfunction()

        function(sloppy_add_conformance_rejected_test test_name case_name source_path expected_error)
            add_test(
                NAME ${test_name}
                COMMAND
                    "${CMAKE_COMMAND}" "-DPROJECT_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
                    "-DCMAKE_BINARY_DIR=${CMAKE_BINARY_DIR}"
                    "-DCARGO_EXECUTABLE=${CARGO_EXECUTABLE}"
                    "-DSLOPPY_CONFORMANCE_CASE=${case_name}" "-DSLOPPY_SOURCE=${source_path}"
                    "-DSLOPPY_EXPECTED_ERROR=${expected_error}" -P
                    "${PROJECT_SOURCE_DIR}/tests/cmake/check_conformance_compile_rejected.cmake")
            set_tests_properties(${test_name} PROPERTIES LABELS "conformance")
        endfunction()

        sloppy_add_conformance_compile_test(
            conformance.hello.compile_artifacts hello examples/compiler-hello/app.js)
        sloppy_add_conformance_compile_test(
            conformance.hello_minimal.compile_artifacts hello-minimal
            examples/hello-minimal/src/main.ts)
        sloppy_add_conformance_compile_test(
            conformance.configured_api.compile_artifacts configured-api
            examples/configured-api/app.js)
        sloppy_add_conformance_compile_test(
            conformance.modules_api.compile_artifacts modules-api examples/modules-api/app.js)
        sloppy_add_conformance_compile_test(
            conformance.validation_errors.compile_artifacts validation-errors
            examples/validation-errors/app.js)
        sloppy_add_conformance_compile_test(
            conformance.request_context.compile_artifacts request-context
            examples/request-context/app.js)
        sloppy_add_conformance_compile_test(
            conformance.http_methods.compile_artifacts http-methods
            compiler/tests/fixtures/http-methods/input.js)
        sloppy_add_conformance_compile_test(
            conformance.async_handler.compile_artifacts async-handler
            compiler/tests/fixtures/async-handler/input.js)
        sloppy_add_conformance_compile_test(
            conformance.provider_capability.compile_artifacts provider-capability
            compiler/tests/fixtures/provider-capability/input.js)
        sloppy_add_conformance_compile_test(
            conformance.users_api_sqlite.compile_artifacts users-api-sqlite
            examples/users-api-sqlite/app.js)
        sloppy_add_conformance_compile_test(
            conformance.prealpha_control_plane.compile_artifacts prealpha-control-plane
            examples/prealpha-control-plane/src/main.js)
        sloppy_add_conformance_compile_test(
            conformance.source_map.compile_artifacts source-map
            compiler/tests/fixtures/source-map/input.js)
        sloppy_add_conformance_compile_test(
            conformance.app_graph_dogfood.compile_artifacts app-graph-dogfood
            compiler/tests/fixtures/app-graph-dogfood/input.ts)
        sloppy_add_conformance_compile_test(
            conformance.dynamic_route.compile_artifacts dynamic-route
            compiler/tests/fixtures/unsupported-dynamic-route/input.js)
        sloppy_add_conformance_rejected_test(
            conformance.unsupported.bare_import bare-import
            compiler/tests/fixtures/unsupported-import-specifier/input.js
            SLOPPYC_E_UNSUPPORTED_IMPORT_SPECIFIER)
        sloppy_add_conformance_rejected_test(
            conformance.unsupported.async_handler_body async-handler-body
            compiler/tests/fixtures/unsupported-async-handler-body/input.js
            SLOPPYC_E_UNSUPPORTED_ASYNC_HANDLER_BODY)
        sloppy_add_conformance_rejected_test(
            conformance.unsupported.secret_capability secret-capability
            compiler/tests/fixtures/unsupported-secret-capability/input.js
            SLOPPYC_E_SECRET_PLAN_METADATA)

        function(sloppy_add_example_tooling_test test_name case_name source_path expected_plan
                 expected_routes expected_capabilities expected_audit expected_openapi)
            set(tooling_command
                "${CMAKE_COMMAND}"
                "-DPROJECT_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
                "-DCMAKE_BINARY_DIR=${CMAKE_BINARY_DIR}"
                "-DCARGO_EXECUTABLE=${CARGO_EXECUTABLE}"
                "-DSLOPPY_CLI=$<TARGET_FILE:sloppy>"
                "-DSLOPPY_CASE=${case_name}"
                "-DSLOPPY_SOURCE=${source_path}")

            if(NOT "${expected_plan}" STREQUAL "")
                list(APPEND tooling_command "-DSLOPPY_EXPECTED_PLAN=${expected_plan}")
            endif()
            if(NOT "${expected_routes}" STREQUAL "")
                list(APPEND tooling_command "-DSLOPPY_EXPECTED_ROUTES=${expected_routes}")
            endif()
            if(NOT "${expected_capabilities}" STREQUAL "")
                list(APPEND tooling_command
                     "-DSLOPPY_EXPECTED_CAPABILITIES=${expected_capabilities}")
            endif()
            if(NOT "${expected_audit}" STREQUAL "")
                list(APPEND tooling_command "-DSLOPPY_EXPECTED_AUDIT=${expected_audit}")
            endif()
            if(NOT "${expected_openapi}" STREQUAL "")
                list(APPEND tooling_command "-DSLOPPY_EXPECTED_OPENAPI=${expected_openapi}")
            endif()
            list(APPEND tooling_command -P
                 "${PROJECT_SOURCE_DIR}/tests/cmake/check_example_tooling.cmake")

            add_test(NAME ${test_name} COMMAND ${tooling_command})
            set_tests_properties(${test_name} PROPERTIES LABELS "conformance;examples")
        endfunction()

        sloppy_add_example_tooling_test(
            examples.hello_minimal.tooling hello-minimal examples/hello-minimal/src/main.ts
            "Hello.Get" "/hello/\\{name\\}" "" "\"findings\"" "x-slop-completeness")
        sloppy_add_example_tooling_test(
            examples.configured_api.tooling configured-api examples/configured-api/app.js
            "App:Greeting" "Config.Get" "" "\"findings\"" "/config")
        sloppy_add_example_tooling_test(
            examples.modules_api.tooling modules-api examples/modules-api/app.js
            "modules" "Users.Get" "" "\"findings\"" "/users/\\{id\\}")
        sloppy_add_example_tooling_test(
            examples.validation_errors.tooling validation-errors examples/validation-errors/app.js
            "UserCreate" "Users.Create" "" "\"findings\"" "UserCreate")
        sloppy_add_example_tooling_test(
            examples.users_api_sqlite.tooling users-api-sqlite examples/users-api-sqlite/app.js
            "users-api-sqlite-runtime.db" "Users.Create" "data.main" "\"findings\""
            "x-slop-capabilities")
        sloppy_add_example_tooling_test(
            examples.prealpha_control_plane.tooling prealpha-control-plane
            examples/prealpha-control-plane/src/main.js "prealpha-control-plane.db"
            "Projects.List" "data.main" "\"findings\"" "x-slop-capabilities")
        sloppy_add_framework_compile_example_tests()
    endif()

    add_test(
        NAME sloppy.cli.missing_metadata
        COMMAND
            "${CMAKE_COMMAND}" "-DSLOPPY_CLI=$<TARGET_FILE:sloppy>"
            "-DSLOPPY_CLI_ARGS=routes;--plan;tests/fixtures/cli/missing.plan.json" -P
            "${PROJECT_SOURCE_DIR}/tests/cmake/check_cli_failure.cmake")
    set_tests_properties(sloppy.cli.missing_metadata PROPERTIES WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}")

    add_test(
        NAME sloppy.cli.openapi_malformed_route
        COMMAND
            "${CMAKE_COMMAND}" "-DSLOPPY_CLI=$<TARGET_FILE:sloppy>"
            "-DSLOPPY_CLI_ARGS=openapi;--plan;tests/fixtures/cli/malformed-route.plan.json"
            "-DSLOPPY_EXPECTED_ERROR=invalid app plan route pattern" -P
            "${PROJECT_SOURCE_DIR}/tests/cmake/check_cli_failure.cmake")
    set_tests_properties(sloppy.cli.openapi_malformed_route
                         PROPERTIES WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}")

    add_test(
        NAME sloppy.run.missing_artifacts
        COMMAND
            "${CMAKE_COMMAND}" "-DSLOPPY_CLI=$<TARGET_FILE:sloppy>"
            "-DSLOPPY_CLI_ARGS=run;--artifacts;tests/fixtures/run/missing;--once;GET;/"
            "-DSLOPPY_EXPECTED_ERROR=artifact path not found" -P
            "${PROJECT_SOURCE_DIR}/tests/cmake/check_cli_failure.cmake")
    set_tests_properties(sloppy.run.missing_artifacts PROPERTIES WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}")

    add_test(
        NAME sloppy.run.missing_plan
        COMMAND
            "${CMAKE_COMMAND}" "-DSLOPPY_CLI=$<TARGET_FILE:sloppy>"
            "-DSLOPPY_CLI_ARGS=run;--artifacts;tests/fixtures/run/missing-plan;--once;GET;/"
            "-DSLOPPY_EXPECTED_ERROR=app.plan.json" -P
            "${PROJECT_SOURCE_DIR}/tests/cmake/check_cli_failure.cmake")
    set_tests_properties(sloppy.run.missing_plan PROPERTIES WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}")

    add_test(
        NAME sloppy.run.malformed_plan
        COMMAND
            "${CMAKE_COMMAND}" "-DSLOPPY_CLI=$<TARGET_FILE:sloppy>"
            "-DSLOPPY_CLI_ARGS=run;--artifacts;tests/fixtures/run/malformed;--once;GET;/"
            "-DSLOPPY_EXPECTED_ERROR=malformed app.plan.json" -P
            "${PROJECT_SOURCE_DIR}/tests/cmake/check_cli_failure.cmake")
    set_tests_properties(sloppy.run.malformed_plan PROPERTIES WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}")

    add_test(
        NAME sloppy.run.invalid_bundle_path
        COMMAND
            "${CMAKE_COMMAND}" "-DSLOPPY_CLI=$<TARGET_FILE:sloppy>"
            "-DSLOPPY_CLI_ARGS=run;--artifacts;tests/fixtures/run/invalid-bundle-path;--once;GET;/"
            "-DSLOPPY_EXPECTED_ERROR=invalid bundle path in app.plan.json" -P
            "${PROJECT_SOURCE_DIR}/tests/cmake/check_cli_failure.cmake")
    set_tests_properties(sloppy.run.invalid_bundle_path
                         PROPERTIES WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}")

    add_test(
        NAME sloppy.run.invalid_source_map_path
        COMMAND
            "${CMAKE_COMMAND}" "-DSLOPPY_CLI=$<TARGET_FILE:sloppy>"
            "-DSLOPPY_CLI_ARGS=run;--artifacts;tests/fixtures/run/invalid-source-map-path;--once;GET;/"
            "-DSLOPPY_EXPECTED_ERROR=invalid source map path in app.plan.json" -P
            "${PROJECT_SOURCE_DIR}/tests/cmake/check_cli_failure.cmake")
    set_tests_properties(sloppy.run.invalid_source_map_path
                         PROPERTIES WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}")

    add_test(
        NAME sloppy.run.hash_mismatch
        COMMAND
            "${CMAKE_COMMAND}" "-DSLOPPY_CLI=$<TARGET_FILE:sloppy>"
            "-DSLOPPY_CLI_ARGS=run;--artifacts;tests/fixtures/run/hash-mismatch;--once;GET;/"
            "-DSLOPPY_EXPECTED_ERROR=bundle hash mismatch" -P
            "${PROJECT_SOURCE_DIR}/tests/cmake/check_cli_failure.cmake")
    set_tests_properties(sloppy.run.hash_mismatch PROPERTIES WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}")

    add_test(
        NAME sloppy.run.source_map_hash_mismatch
        COMMAND
            "${CMAKE_COMMAND}" "-DSLOPPY_CLI=$<TARGET_FILE:sloppy>"
            "-DSLOPPY_CLI_ARGS=run;--artifacts;tests/fixtures/run/source-map-hash-mismatch;--once;GET;/"
            "-DSLOPPY_EXPECTED_ERROR=source map hash mismatch" -P
            "${PROJECT_SOURCE_DIR}/tests/cmake/check_cli_failure.cmake")
    set_tests_properties(sloppy.run.source_map_hash_mismatch
                         PROPERTIES WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}")

    add_test(
        NAME sloppy.run.missing_source_map
        COMMAND
            "${CMAKE_COMMAND}" "-DSLOPPY_CLI=$<TARGET_FILE:sloppy>"
            "-DSLOPPY_CLI_ARGS=run;--artifacts;tests/fixtures/run/missing-source-map;--once;GET;/"
            "-DSLOPPY_EXPECTED_ERROR=artifact path not found" -P
            "${PROJECT_SOURCE_DIR}/tests/cmake/check_cli_failure.cmake")
    set_tests_properties(sloppy.run.missing_source_map
                         PROPERTIES WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}")

    add_test(
        NAME sloppy.run.too_many_routes
        COMMAND
            "${CMAKE_COMMAND}" "-DSLOPPY_CLI=$<TARGET_FILE:sloppy>"
            "-DSLOPPY_CLI_ARGS=run;--artifacts;tests/fixtures/run/too-many-routes;--once;GET;/"
            "-DSLOPPY_EXPECTED_ERROR=app plan has too many runnable routes"
            -P "${PROJECT_SOURCE_DIR}/tests/cmake/check_cli_failure.cmake")
    set_tests_properties(sloppy.run.too_many_routes
                         PROPERTIES WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}")

    add_test(
        NAME sloppy.cli.max_routes_plan
        COMMAND
            "${CMAKE_COMMAND}" "-DPROJECT_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
            "-DSLOPPY_TEST_BINARY_DIR=${CMAKE_BINARY_DIR}"
            "-DSLOPPY_CLI=$<TARGET_FILE:sloppy>"
            "-DSLOPPY_ROUTE_COUNT=1024" -P
            "${PROJECT_SOURCE_DIR}/tests/cmake/check_max_routes_plan.cmake")
    set_tests_properties(sloppy.cli.max_routes_plan
                         PROPERTIES WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}")

    add_test(
        NAME sloppy.run.version_mismatch
        COMMAND
            "${CMAKE_COMMAND}" "-DSLOPPY_CLI=$<TARGET_FILE:sloppy>"
            "-DSLOPPY_CLI_ARGS=run;--artifacts;tests/fixtures/run/version-mismatch;--once;GET;/"
            "-DSLOPPY_EXPECTED_ERROR=unsupported app.plan.json runtimeMinimumVersion" -P
            "${PROJECT_SOURCE_DIR}/tests/cmake/check_cli_failure.cmake")
    set_tests_properties(sloppy.run.version_mismatch
                         PROPERTIES WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}")

    add_test(
        NAME sloppy.run.rejects_mixed_artifact_inputs
        COMMAND
            "${CMAKE_COMMAND}" "-DSLOPPY_CLI=$<TARGET_FILE:sloppy>"
            "-DSLOPPY_CLI_ARGS=run;--artifacts;tests/integration/execution/compiler_artifact;extra;--once;GET;/"
            "-DSLOPPY_EXPECTED_ERROR=expected either --artifacts <dir> or one positional source or artifact path"
            -P "${PROJECT_SOURCE_DIR}/tests/cmake/check_cli_failure.cmake")
    set_tests_properties(sloppy.run.rejects_mixed_artifact_inputs
                         PROPERTIES WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}")

    add_test(
        NAME sloppy.run.rejects_environment_with_artifacts
        COMMAND
            "${CMAKE_COMMAND}" "-DSLOPPY_CLI=$<TARGET_FILE:sloppy>"
            "-DSLOPPY_CLI_ARGS=run;--artifacts;tests/integration/execution/compiler_artifact;--environment;Development;--once;GET;/"
            "-DSLOPPY_EXPECTED_ERROR=--environment only applies to source input or sloppy.json"
            -P "${PROJECT_SOURCE_DIR}/tests/cmake/check_cli_failure.cmake")
    set_tests_properties(sloppy.run.rejects_environment_with_artifacts
                         PROPERTIES WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}")
