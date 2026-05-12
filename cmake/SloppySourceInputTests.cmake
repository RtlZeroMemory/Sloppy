# Source-input and runtime-artifact execution tests. Included by cmake/SloppyTests.cmake.

    if(CARGO_EXECUTABLE AND SLOPPY_BUILD_COMPILER)
        if(SLOPPY_POWERSHELL_EXECUTABLE)
            if(SLOPPY_ENABLE_V8)
                add_test(
                    NAME conformance.source_input.fixture_harness
                    COMMAND
                        "${SLOPPY_POWERSHELL_EXECUTABLE}" ${SLOPPY_POWERSHELL_ARGS}
                        "${PROJECT_SOURCE_DIR}/tools/windows/test-source-input-fixtures.ps1"
                        -SloppyExe "$<TARGET_FILE:sloppy>" -SloppycExe
                        "${SLOPPYC_BUILT_EXECUTABLE}" -FixtureRoot
                        "${PROJECT_SOURCE_DIR}/tests/fixtures/source-input" -WorkRoot
                        "${CMAKE_BINARY_DIR}/source-input-fixtures" -RequireV8Runtime)
                set_tests_properties(conformance.source_input.fixture_harness
                                     PROPERTIES LABELS "conformance;source-input;v8")
            else()
                add_test(
                    NAME conformance.source_input.fixture_harness
                    COMMAND
                        "${SLOPPY_POWERSHELL_EXECUTABLE}" ${SLOPPY_POWERSHELL_ARGS}
                        "${PROJECT_SOURCE_DIR}/tools/windows/test-source-input-fixtures.ps1"
                        -SloppyExe "$<TARGET_FILE:sloppy>" -SloppycExe
                        "${SLOPPYC_BUILT_EXECUTABLE}" -FixtureRoot
                        "${PROJECT_SOURCE_DIR}/tests/fixtures/source-input" -WorkRoot
                        "${CMAKE_BINARY_DIR}/source-input-fixtures")
                set_tests_properties(conformance.source_input.fixture_harness
                                     PROPERTIES LABELS "conformance;source-input")
            endif()
        else()
            message(WARNING "PowerShell host not found; source-input fixture harness is unavailable")
        endif()

        add_test(
            NAME conformance.static_assets.package_and_run
            COMMAND
                "${CMAKE_COMMAND}" "-DPROJECT_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
                "-DCMAKE_BINARY_DIR=${CMAKE_BINARY_DIR}" "-DSLOPPY_CLI=$<TARGET_FILE:sloppy>"
                "-DSLOPPYC_EXECUTABLE=${SLOPPYC_BUILT_EXECUTABLE}"
                "-DSLOPPY_ENABLE_V8=${SLOPPY_ENABLE_V8}" -P
                "${PROJECT_SOURCE_DIR}/tests/cmake/check_static_assets.cmake")
        set_tests_properties(
            conformance.static_assets.package_and_run
            PROPERTIES LABELS "conformance;source-input;static;asset;package")

        if(NOT SLOPPY_ENABLE_V8)
            add_test(
                NAME sloppy.run.source_input_emits_artifacts_non_v8
                COMMAND
                    "${CMAKE_COMMAND}" "-DPROJECT_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
                    "-DCMAKE_BINARY_DIR=${CMAKE_BINARY_DIR}" "-DSLOPPY_CLI=$<TARGET_FILE:sloppy>"
                    "-DSLOPPYC_EXECUTABLE=${SLOPPYC_BUILT_EXECUTABLE}"
                    "-DSLOPPY_CASE=hello-non-v8" "-DSLOPPY_SOURCE=examples/compiler-hello/app.js"
                    "-DSLOPPY_ONCE_METHOD=GET" "-DSLOPPY_ONCE_TARGET=/"
                    "-DSLOPPY_EXPECTED_ERROR=requires V8-enabled build"
                    "-DSLOPPY_EXPECTED_ARTIFACT_DIR=.sloppy" -P
                    "${PROJECT_SOURCE_DIR}/tests/cmake/check_source_input_run.cmake")
            set_tests_properties(sloppy.run.source_input_emits_artifacts_non_v8
                                 PROPERTIES LABELS "source-input")

            add_test(
                NAME sloppy.run.project_config_emits_artifacts_non_v8
                COMMAND
                    "${CMAKE_COMMAND}" "-DPROJECT_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
                    "-DCMAKE_BINARY_DIR=${CMAKE_BINARY_DIR}" "-DSLOPPY_CLI=$<TARGET_FILE:sloppy>"
                    "-DSLOPPYC_EXECUTABLE=${SLOPPYC_BUILT_EXECUTABLE}"
                    "-DSLOPPY_CASE=config-non-v8"
                    "-DSLOPPY_CONFIG_ENTRY={\"entry\":\"app.js\",\"outDir\":\".sloppy\",\"environment\":\"Development\"}"
                    "-DSLOPPY_CONFIG_SOURCE=examples/compiler-hello/app.js"
                    "-DSLOPPY_ONCE_METHOD=GET" "-DSLOPPY_ONCE_TARGET=/"
                    "-DSLOPPY_EXPECTED_ERROR=requires V8-enabled build"
                    "-DSLOPPY_EXPECTED_ARTIFACT_DIR=.sloppy" -P
                    "${PROJECT_SOURCE_DIR}/tests/cmake/check_source_input_run.cmake")
            set_tests_properties(sloppy.run.project_config_emits_artifacts_non_v8
                                 PROPERTIES LABELS "source-input")

            add_test(
                NAME sloppy.run.project_config_shell_metacharacters_non_v8
                COMMAND
                    "${CMAKE_COMMAND}" "-DPROJECT_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
                    "-DCMAKE_BINARY_DIR=${CMAKE_BINARY_DIR}" "-DSLOPPY_CLI=$<TARGET_FILE:sloppy>"
                    "-DSLOPPYC_EXECUTABLE=${SLOPPYC_BUILT_EXECUTABLE}"
                    "-DSLOPPY_CASE=config-metacharacters-non-v8"
                    "-DSLOPPY_CONFIG_ENTRY={\"entry\":\"app.js\",\"outDir\":\".sloppy/$(not-a-command)\",\"environment\":\"Development\"}"
                    "-DSLOPPY_CONFIG_SOURCE=examples/compiler-hello/app.js"
                    "-DSLOPPY_ONCE_METHOD=GET" "-DSLOPPY_ONCE_TARGET=/"
                    "-DSLOPPY_EXPECTED_ERROR=requires V8-enabled build"
                    "-DSLOPPY_EXPECTED_ARTIFACT_DIR=.sloppy/$(not-a-command)" -P
                    "${PROJECT_SOURCE_DIR}/tests/cmake/check_source_input_run.cmake")
            set_tests_properties(sloppy.run.project_config_shell_metacharacters_non_v8
                                 PROPERTIES LABELS "source-input")

            add_test(
                NAME sloppy.run.source_input_config_driven_sqlite_non_v8
                COMMAND
                    "${CMAKE_COMMAND}" "-DPROJECT_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
                    "-DCMAKE_BINARY_DIR=${CMAKE_BINARY_DIR}" "-DSLOPPY_CLI=$<TARGET_FILE:sloppy>"
                    "-DSLOPPYC_EXECUTABLE=${SLOPPYC_BUILT_EXECUTABLE}"
                    "-DSLOPPY_CASE=config-sqlite-non-v8"
                    "-DSLOPPY_SOURCE=examples/users-api-sqlite/app.js" "-DSLOPPY_ONCE_METHOD=GET"
                    "-DSLOPPY_ONCE_TARGET=/users"
                    "-DSLOPPY_ENVIRONMENT=Development"
                    "-DSLOPPY_EXPECTED_ERROR=requires V8-enabled build"
                    "-DSLOPPY_EXPECTED_ARTIFACT_DIR=.sloppy"
                    "-DSLOPPY_EXPECTED_PLAN=users-api-sqlite-runtime.db" -P
                    "${PROJECT_SOURCE_DIR}/tests/cmake/check_source_input_run.cmake")
            set_tests_properties(sloppy.run.source_input_config_driven_sqlite_non_v8
                                 PROPERTIES LABELS "source-input")
            add_test(
                NAME sloppy.run.hello_minimal_source_input_non_v8
                COMMAND
                    "${CMAKE_COMMAND}" "-DPROJECT_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
                    "-DCMAKE_BINARY_DIR=${CMAKE_BINARY_DIR}" "-DSLOPPY_CLI=$<TARGET_FILE:sloppy>"
                    "-DSLOPPYC_EXECUTABLE=${SLOPPYC_BUILT_EXECUTABLE}"
                    "-DSLOPPY_CASE=hello-minimal-non-v8"
                    "-DSLOPPY_SOURCE=examples/hello-minimal/src/main.ts" "-DSLOPPY_ONCE_METHOD=GET"
                    "-DSLOPPY_ONCE_TARGET=/hello/Ada"
                    "-DSLOPPY_EXPECTED_ERROR=requires V8-enabled build"
                    "-DSLOPPY_EXPECTED_ARTIFACT_DIR=.sloppy"
                    "-DSLOPPY_EXPECTED_PLAN=Hello.Get" -P
                    "${PROJECT_SOURCE_DIR}/tests/cmake/check_source_input_run.cmake")
            set_tests_properties(sloppy.run.hello_minimal_source_input_non_v8
                                 PROPERTIES LABELS "source-input")
            add_test(
                NAME sloppy.build.project_config_multifile_non_v8
                COMMAND
                    "${CMAKE_COMMAND}" "-DPROJECT_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
                    "-DCMAKE_BINARY_DIR=${CMAKE_BINARY_DIR}" "-DSLOPPY_CLI=$<TARGET_FILE:sloppy>"
                    "-DSLOPPYC_EXECUTABLE=${SLOPPYC_BUILT_EXECUTABLE}"
                    "-DSLOPPY_COMMAND=build" "-DSLOPPY_CASE=project-multifile-build-non-v8"
                    "-DSLOPPY_PROJECT_FIXTURE=tests/fixtures/source-input/project-multifile"
                    "-DSLOPPY_EXPECTED_ARTIFACT_DIR=.sloppy"
                    "-DSLOPPY_EXPECTED_PLAN=appsettings.Development.json"
                    "-DSLOPPY_EXPECTED_SOURCE_MAP=users.module.ts" -P
                    "${PROJECT_SOURCE_DIR}/tests/cmake/check_source_input_run.cmake")
            set_tests_properties(sloppy.build.project_config_multifile_non_v8
                                 PROPERTIES LABELS "source-input")
            add_test(
                NAME sloppy.build.prealpha_control_plane_project_non_v8
                COMMAND
                    "${CMAKE_COMMAND}" "-DPROJECT_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
                    "-DCMAKE_BINARY_DIR=${CMAKE_BINARY_DIR}" "-DSLOPPY_CLI=$<TARGET_FILE:sloppy>"
                    "-DSLOPPYC_EXECUTABLE=${SLOPPYC_BUILT_EXECUTABLE}"
                    "-DSLOPPY_COMMAND=build" "-DSLOPPY_CASE=prealpha-control-plane-build-non-v8"
                    "-DSLOPPY_PROJECT_FIXTURE=examples/prealpha-control-plane"
                    "-DSLOPPY_EXPECTED_ARTIFACT_DIR=.sloppy"
                    "-DSLOPPY_EXPECTED_PLAN=Projects.List"
                    "-DSLOPPY_EXPECTED_SOURCE_MAP=projects.js" -P
                    "${PROJECT_SOURCE_DIR}/tests/cmake/check_source_input_run.cmake")
            set_tests_properties(sloppy.build.prealpha_control_plane_project_non_v8
                                 PROPERTIES LABELS "source-input;dogfood;sqlite")
            add_test(
                NAME sloppy.build.source_input_non_v8
                COMMAND
                    "${CMAKE_COMMAND}" "-DPROJECT_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
                    "-DCMAKE_BINARY_DIR=${CMAKE_BINARY_DIR}" "-DSLOPPY_CLI=$<TARGET_FILE:sloppy>"
                    "-DSLOPPYC_EXECUTABLE=${SLOPPYC_BUILT_EXECUTABLE}"
                    "-DSLOPPY_COMMAND=build" "-DSLOPPY_CASE=hello-minimal-build-non-v8"
                    "-DSLOPPY_SOURCE=examples/hello-minimal/src/main.ts"
                    "-DSLOPPY_EXPECTED_ARTIFACT_DIR=.sloppy"
                    "-DSLOPPY_EXPECTED_PLAN=Hello.Get" -P
                    "${PROJECT_SOURCE_DIR}/tests/cmake/check_source_input_run.cmake")
            set_tests_properties(sloppy.build.source_input_non_v8
                                 PROPERTIES LABELS "source-input")
            add_test(
                NAME sloppy.build.source_input_out_non_v8
                COMMAND
                    "${CMAKE_COMMAND}" "-DPROJECT_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
                    "-DCMAKE_BINARY_DIR=${CMAKE_BINARY_DIR}" "-DSLOPPY_CLI=$<TARGET_FILE:sloppy>"
                    "-DSLOPPYC_EXECUTABLE=${SLOPPYC_BUILT_EXECUTABLE}"
                    "-DSLOPPY_COMMAND=build" "-DSLOPPY_CASE=hello-minimal-build-out-non-v8"
                    "-DSLOPPY_SOURCE=examples/hello-minimal/src/main.ts"
                    "-DSLOPPY_OUT_DIR=.sloppy/custom-build"
                    "-DSLOPPY_EXPECTED_ARTIFACT_DIR=.sloppy/custom-build"
                    "-DSLOPPY_EXPECTED_PLAN=Hello.Get" -P
                    "${PROJECT_SOURCE_DIR}/tests/cmake/check_source_input_run.cmake")
            set_tests_properties(sloppy.build.source_input_out_non_v8
                                 PROPERTIES LABELS "source-input")
            add_test(
                NAME sloppy.run.project_config_multifile_non_v8
                COMMAND
                    "${CMAKE_COMMAND}" "-DPROJECT_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
                    "-DCMAKE_BINARY_DIR=${CMAKE_BINARY_DIR}" "-DSLOPPY_CLI=$<TARGET_FILE:sloppy>"
                    "-DSLOPPYC_EXECUTABLE=${SLOPPYC_BUILT_EXECUTABLE}"
                    "-DSLOPPY_CASE=project-multifile-run-non-v8"
                    "-DSLOPPY_PROJECT_FIXTURE=tests/fixtures/source-input/project-multifile"
                    "-DSLOPPY_ONCE_METHOD=GET" "-DSLOPPY_ONCE_TARGET=/users/ada"
                    "-DSLOPPY_EXPECTED_ERROR=requires V8-enabled build"
                    "-DSLOPPY_EXPECTED_ARTIFACT_DIR=.sloppy"
                    "-DSLOPPY_EXPECTED_PLAN=appsettings.Development.json"
                    "-DSLOPPY_EXPECTED_SOURCE_MAP=users.module.ts" -P
                    "${PROJECT_SOURCE_DIR}/tests/cmake/check_source_input_run.cmake")
            set_tests_properties(sloppy.run.project_config_multifile_non_v8
                                 PROPERTIES LABELS "source-input")
            add_test(
                NAME sloppy.run.prealpha_control_plane_project_non_v8
                COMMAND
                    "${CMAKE_COMMAND}" "-DPROJECT_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
                    "-DCMAKE_BINARY_DIR=${CMAKE_BINARY_DIR}" "-DSLOPPY_CLI=$<TARGET_FILE:sloppy>"
                    "-DSLOPPYC_EXECUTABLE=${SLOPPYC_BUILT_EXECUTABLE}"
                    "-DSLOPPY_CASE=prealpha-control-plane-run-non-v8"
                    "-DSLOPPY_PROJECT_FIXTURE=examples/prealpha-control-plane"
                    "-DSLOPPY_ONCE_METHOD=GET" "-DSLOPPY_ONCE_TARGET=/projects?owner=runtime"
                    "-DSLOPPY_EXPECTED_ERROR=requires V8-enabled build"
                    "-DSLOPPY_EXPECTED_ARTIFACT_DIR=.sloppy"
                    "-DSLOPPY_EXPECTED_PLAN=Projects.List"
                    "-DSLOPPY_EXPECTED_SOURCE_MAP=projects.js" -P
                    "${PROJECT_SOURCE_DIR}/tests/cmake/check_source_input_run.cmake")
            set_tests_properties(sloppy.run.prealpha_control_plane_project_non_v8
                                 PROPERTIES LABELS "source-input;dogfood;sqlite")
        endif()

        add_test(
            NAME sloppy.build.program_project_capabilities_non_v8
            COMMAND
                "${CMAKE_COMMAND}" "-DPROJECT_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
                "-DCMAKE_BINARY_DIR=${CMAKE_BINARY_DIR}" "-DSLOPPY_CLI=$<TARGET_FILE:sloppy>"
                "-DSLOPPYC_EXECUTABLE=${SLOPPYC_BUILT_EXECUTABLE}" "-DSLOPPY_COMMAND=build"
                "-DSLOPPY_CASE=program-project-capabilities"
                "-DSLOPPY_PROJECT_FIXTURE=examples/program-fs-process"
                "-DSLOPPY_EXPECTED_ARTIFACT_DIR=.sloppy" "-DSLOPPY_EXPECTED_PLAN=\"token\": \"fs\""
                "-DSLOPPY_EXPECTED_PLAN_SECONDARY=\"token\": \"os\""
                "-DSLOPPY_EXPECTED_SOURCE_MAP=programModules" -P
                "${PROJECT_SOURCE_DIR}/tests/cmake/check_source_input_run.cmake")
        set_tests_properties(sloppy.build.program_project_capabilities_non_v8
                             PROPERTIES LABELS "source-input;program-mode")

        add_test(
            NAME sloppy.program_artifact_shorthand
            COMMAND
                "${CMAKE_COMMAND}" "-DPROJECT_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
                "-DCMAKE_BINARY_DIR=${CMAKE_BINARY_DIR}" "-DSLOPPY_CLI=$<TARGET_FILE:sloppy>"
                "-DSLOPPYC_EXECUTABLE=${SLOPPYC_BUILT_EXECUTABLE}"
                "-DSLOPPY_EXPECT_RUN_SUCCESS=${SLOPPY_ENABLE_V8}" -P
                "${PROJECT_SOURCE_DIR}/tests/cmake/check_program_artifact_shorthand.cmake")
        set_tests_properties(sloppy.program_artifact_shorthand
                             PROPERTIES LABELS "source-input;program-mode")

        add_test(
            NAME sloppy.node_compat.package_fixtures
            COMMAND
                "${CMAKE_COMMAND}" "-DPROJECT_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
                "-DCMAKE_BINARY_DIR=${CMAKE_BINARY_DIR}" "-DSLOPPY_CLI=$<TARGET_FILE:sloppy>"
                "-DSLOPPYC_EXECUTABLE=${SLOPPYC_BUILT_EXECUTABLE}"
                "-DSLOPPY_EXPECT_RUN_SUCCESS=${SLOPPY_ENABLE_V8}" -P
                "${PROJECT_SOURCE_DIR}/tests/cmake/check_node_compat_packages.cmake")
        set_tests_properties(sloppy.node_compat.package_fixtures
                             PROPERTIES LABELS "source-input;program-mode;node_compat;package;deps")

        add_test(
            NAME sloppy.npm_compat.representative_fixtures
            COMMAND
                "${CMAKE_COMMAND}" "-DPROJECT_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
                "-DCMAKE_BINARY_DIR=${CMAKE_BINARY_DIR}" "-DSLOPPY_CLI=$<TARGET_FILE:sloppy>"
                "-DSLOPPYC_EXECUTABLE=${SLOPPYC_BUILT_EXECUTABLE}"
                "-DSLOPPY_EXPECT_RUN_SUCCESS=${SLOPPY_ENABLE_V8}" -P
                "${PROJECT_SOURCE_DIR}/tests/cmake/check_npm_compat_representatives.cmake")
        set_tests_properties(sloppy.npm_compat.representative_fixtures
                             PROPERTIES LABELS "source-input;program-mode;npm_compat;package;deps")

        add_test(
            NAME sloppy.npm_runtime.behavior_fixtures
            COMMAND
                "${CMAKE_COMMAND}" "-DPROJECT_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
                "-DCMAKE_BINARY_DIR=${CMAKE_BINARY_DIR}" "-DSLOPPY_CLI=$<TARGET_FILE:sloppy>"
                "-DSLOPPYC_EXECUTABLE=${SLOPPYC_BUILT_EXECUTABLE}"
                "-DSLOPPY_EXPECT_RUN_SUCCESS=${SLOPPY_ENABLE_V8}" -P
                "${PROJECT_SOURCE_DIR}/tests/cmake/check_npm_runtime_behavior_fixtures.cmake")
        set_tests_properties(sloppy.npm_runtime.behavior_fixtures
                             PROPERTIES LABELS "source-input;program-mode;npm_runtime;npm_compat;package;deps")

        add_test(
            NAME sloppy.build.missing_project_config
            COMMAND
                "${CMAKE_COMMAND}" "-DPROJECT_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
                "-DCMAKE_BINARY_DIR=${CMAKE_BINARY_DIR}" "-DSLOPPY_CLI=$<TARGET_FILE:sloppy>"
                "-DSLOPPYC_EXECUTABLE=${SLOPPYC_BUILT_EXECUTABLE}" "-DSLOPPY_COMMAND=build"
                "-DSLOPPY_CASE=build-missing-config"
                "-DSLOPPY_EXPECTED_ERROR=project config not found" -P
                "${PROJECT_SOURCE_DIR}/tests/cmake/check_source_input_run.cmake")
        set_tests_properties(sloppy.build.missing_project_config
                             PROPERTIES LABELS "source-input")

        add_test(
            NAME sloppy.run.missing_project_config
            COMMAND
                "${CMAKE_COMMAND}" "-DPROJECT_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
                "-DCMAKE_BINARY_DIR=${CMAKE_BINARY_DIR}" "-DSLOPPY_CLI=$<TARGET_FILE:sloppy>"
                "-DSLOPPYC_EXECUTABLE=${SLOPPYC_BUILT_EXECUTABLE}"
                "-DSLOPPY_CASE=run-missing-config"
                "-DSLOPPY_EXPECTED_ERROR=project config not found" -P
                "${PROJECT_SOURCE_DIR}/tests/cmake/check_source_input_run.cmake")
        set_tests_properties(sloppy.run.missing_project_config
                             PROPERTIES LABELS "source-input")

        add_test(
            NAME sloppy.run.source_input_missing_source
            COMMAND
                "${CMAKE_COMMAND}" "-DPROJECT_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
                "-DCMAKE_BINARY_DIR=${CMAKE_BINARY_DIR}" "-DSLOPPY_CLI=$<TARGET_FILE:sloppy>"
                "-DSLOPPYC_EXECUTABLE=${SLOPPYC_BUILT_EXECUTABLE}"
                "-DSLOPPY_CASE=missing-source" "-DSLOPPY_SOURCE=tests/fixtures/run/missing.js"
                "-DSLOPPY_EXPECTED_ERROR=missing source file" -P
                "${PROJECT_SOURCE_DIR}/tests/cmake/check_source_input_run.cmake")
        set_tests_properties(sloppy.run.source_input_missing_source
                             PROPERTIES LABELS "source-input")

        add_test(
            NAME sloppy.run.source_input_invalid_config
            COMMAND
                "${CMAKE_COMMAND}" "-DPROJECT_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
                "-DCMAKE_BINARY_DIR=${CMAKE_BINARY_DIR}" "-DSLOPPY_CLI=$<TARGET_FILE:sloppy>"
                "-DSLOPPYC_EXECUTABLE=${SLOPPYC_BUILT_EXECUTABLE}"
                "-DSLOPPY_CASE=invalid-config" "-DSLOPPY_CONFIG_ENTRY={"
                "-DSLOPPY_EXPECTED_ERROR=invalid sloppy.json" -P
                "${PROJECT_SOURCE_DIR}/tests/cmake/check_source_input_run.cmake")
        set_tests_properties(sloppy.run.source_input_invalid_config
                             PROPERTIES LABELS "source-input")

        add_test(
            NAME sloppy.run.source_input_missing_entry
            COMMAND
                "${CMAKE_COMMAND}" "-DPROJECT_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
                "-DCMAKE_BINARY_DIR=${CMAKE_BINARY_DIR}" "-DSLOPPY_CLI=$<TARGET_FILE:sloppy>"
                "-DSLOPPYC_EXECUTABLE=${SLOPPYC_BUILT_EXECUTABLE}"
                "-DSLOPPY_CASE=missing-entry" "-DSLOPPY_CONFIG_ENTRY={\"outDir\":\".sloppy\"}"
                "-DSLOPPY_EXPECTED_ERROR=missing entry" -P
                "${PROJECT_SOURCE_DIR}/tests/cmake/check_source_input_run.cmake")
        set_tests_properties(sloppy.run.source_input_missing_entry
                             PROPERTIES LABELS "source-input")

        add_test(
            NAME sloppy.run.source_input_invalid_entry
            COMMAND
                "${CMAKE_COMMAND}" "-DPROJECT_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
                "-DCMAKE_BINARY_DIR=${CMAKE_BINARY_DIR}" "-DSLOPPY_CLI=$<TARGET_FILE:sloppy>"
                "-DSLOPPYC_EXECUTABLE=${SLOPPYC_BUILT_EXECUTABLE}"
                "-DSLOPPY_CASE=invalid-entry" "-DSLOPPY_CONFIG_ENTRY={\"entry\":123}"
                "-DSLOPPY_EXPECTED_ERROR=entry must be a non-empty string" -P
                "${PROJECT_SOURCE_DIR}/tests/cmake/check_source_input_run.cmake")
        set_tests_properties(sloppy.run.source_input_invalid_entry
                             PROPERTIES LABELS "source-input")

        add_test(
            NAME sloppy.build.source_input_missing_entry
            COMMAND
                "${CMAKE_COMMAND}" "-DPROJECT_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
                "-DCMAKE_BINARY_DIR=${CMAKE_BINARY_DIR}" "-DSLOPPY_CLI=$<TARGET_FILE:sloppy>"
                "-DSLOPPYC_EXECUTABLE=${SLOPPYC_BUILT_EXECUTABLE}" "-DSLOPPY_COMMAND=build"
                "-DSLOPPY_CASE=build-missing-entry" "-DSLOPPY_CONFIG_ENTRY={\"outDir\":\".sloppy\"}"
                "-DSLOPPY_EXPECTED_ERROR=missing entry" -P
                "${PROJECT_SOURCE_DIR}/tests/cmake/check_source_input_run.cmake")
        set_tests_properties(sloppy.build.source_input_missing_entry
                             PROPERTIES LABELS "source-input")

        add_test(
            NAME sloppy.build.source_input_invalid_entry
            COMMAND
                "${CMAKE_COMMAND}" "-DPROJECT_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
                "-DCMAKE_BINARY_DIR=${CMAKE_BINARY_DIR}" "-DSLOPPY_CLI=$<TARGET_FILE:sloppy>"
                "-DSLOPPYC_EXECUTABLE=${SLOPPYC_BUILT_EXECUTABLE}" "-DSLOPPY_COMMAND=build"
                "-DSLOPPY_CASE=build-invalid-entry" "-DSLOPPY_CONFIG_ENTRY={\"entry\":123}"
                "-DSLOPPY_EXPECTED_ERROR=entry must be a non-empty string" -P
                "${PROJECT_SOURCE_DIR}/tests/cmake/check_source_input_run.cmake")
        set_tests_properties(sloppy.build.source_input_invalid_entry
                             PROPERTIES LABELS "source-input")

        add_test(
            NAME sloppy.build.artifacts_rejected
            COMMAND
                "${CMAKE_COMMAND}" "-DPROJECT_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
                "-DCMAKE_BINARY_DIR=${CMAKE_BINARY_DIR}" "-DSLOPPY_CLI=$<TARGET_FILE:sloppy>"
                "-DSLOPPYC_EXECUTABLE=${SLOPPYC_BUILT_EXECUTABLE}" "-DSLOPPY_COMMAND=build"
                "-DSLOPPY_CASE=build-artifacts-rejected"
                "-DSLOPPY_EXTRA_ARGS=--artifacts;tests/integration/execution/compiler_artifact"
                "-DSLOPPY_EXPECTED_ERROR=--artifacts is not supported" -P
                "${PROJECT_SOURCE_DIR}/tests/cmake/check_source_input_run.cmake")
        set_tests_properties(sloppy.build.artifacts_rejected
                             PROPERTIES LABELS "source-input")

        add_test(
            NAME sloppy.build.source_input_unsupported_config_field
            COMMAND
                "${CMAKE_COMMAND}" "-DPROJECT_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
                "-DCMAKE_BINARY_DIR=${CMAKE_BINARY_DIR}" "-DSLOPPY_CLI=$<TARGET_FILE:sloppy>"
                "-DSLOPPYC_EXECUTABLE=${SLOPPYC_BUILT_EXECUTABLE}" "-DSLOPPY_COMMAND=build"
                "-DSLOPPY_CASE=build-unsupported-field"
                "-DSLOPPY_CONFIG_ENTRY={\"entry\":\"app.js\",\"package\":\"nope\"}"
                "-DSLOPPY_CONFIG_SOURCE=examples/compiler-hello/app.js"
                "-DSLOPPY_EXPECTED_ERROR=unsupported field" -P
                "${PROJECT_SOURCE_DIR}/tests/cmake/check_source_input_run.cmake")
        set_tests_properties(sloppy.build.source_input_unsupported_config_field
                             PROPERTIES LABELS "source-input")

        add_test(
            NAME sloppy.build.source_input_entry_escape
            COMMAND
                "${CMAKE_COMMAND}" "-DPROJECT_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
                "-DCMAKE_BINARY_DIR=${CMAKE_BINARY_DIR}" "-DSLOPPY_CLI=$<TARGET_FILE:sloppy>"
                "-DSLOPPYC_EXECUTABLE=${SLOPPYC_BUILT_EXECUTABLE}" "-DSLOPPY_COMMAND=build"
                "-DSLOPPY_CASE=build-entry-escape"
                "-DSLOPPY_CONFIG_ENTRY={\"entry\":\"../outside.ts\",\"outDir\":\".sloppy\"}"
                "-DSLOPPY_EXPECTED_ERROR=entry must be a relative path inside the project root" -P
                "${PROJECT_SOURCE_DIR}/tests/cmake/check_source_input_run.cmake")
        set_tests_properties(sloppy.build.source_input_entry_escape
                             PROPERTIES LABELS "source-input")

        add_test(
            NAME sloppy.run.source_input_compiler_failure
            COMMAND
                "${CMAKE_COMMAND}" "-DPROJECT_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
                "-DCMAKE_BINARY_DIR=${CMAKE_BINARY_DIR}" "-DSLOPPY_CLI=$<TARGET_FILE:sloppy>"
                "-DSLOPPYC_EXECUTABLE=${SLOPPYC_BUILT_EXECUTABLE}"
                "-DSLOPPY_CASE=compiler-failure"
                "-DSLOPPY_SOURCE=compiler/tests/fixtures/unsupported-dynamic-import/input.js"
                "-DSLOPPY_EXPECTED_ERROR=UNSUPPORTED_DYNAMIC_IMPORT" -P
                "${PROJECT_SOURCE_DIR}/tests/cmake/check_source_input_run.cmake")
        set_tests_properties(sloppy.run.source_input_compiler_failure
                             PROPERTIES LABELS "source-input")

        add_test(
            NAME sloppy.build.source_input_compiler_failure
            COMMAND
                "${CMAKE_COMMAND}" "-DPROJECT_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
                "-DCMAKE_BINARY_DIR=${CMAKE_BINARY_DIR}" "-DSLOPPY_CLI=$<TARGET_FILE:sloppy>"
                "-DSLOPPYC_EXECUTABLE=${SLOPPYC_BUILT_EXECUTABLE}" "-DSLOPPY_COMMAND=build"
                "-DSLOPPY_CASE=build-compiler-failure"
                "-DSLOPPY_SOURCE=compiler/tests/fixtures/unsupported-dynamic-import/input.js"
                "-DSLOPPY_EXPECTED_ERROR=UNSUPPORTED_DYNAMIC_IMPORT" -P
                "${PROJECT_SOURCE_DIR}/tests/cmake/check_source_input_run.cmake")
        set_tests_properties(sloppy.build.source_input_compiler_failure
                             PROPERTIES LABELS "source-input")

        add_test(
            NAME sloppy.build.source_input_missing_relative_module
            COMMAND
                "${CMAKE_COMMAND}" "-DPROJECT_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
                "-DCMAKE_BINARY_DIR=${CMAKE_BINARY_DIR}" "-DSLOPPY_CLI=$<TARGET_FILE:sloppy>"
                "-DSLOPPYC_EXECUTABLE=${SLOPPYC_BUILT_EXECUTABLE}" "-DSLOPPY_COMMAND=build"
                "-DSLOPPY_CASE=build-missing-relative"
                "-DSLOPPY_SOURCE=compiler/tests/fixtures/missing-relative-import/input.js"
                "-DSLOPPY_EXPECTED_ERROR=MISSING_RELATIVE_IMPORT" -P
                "${PROJECT_SOURCE_DIR}/tests/cmake/check_source_input_run.cmake")
        set_tests_properties(sloppy.build.source_input_missing_relative_module
                             PROPERTIES LABELS "source-input")

        add_test(
            NAME sloppy.build.source_input_bare_import
            COMMAND
                "${CMAKE_COMMAND}" "-DPROJECT_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
                "-DCMAKE_BINARY_DIR=${CMAKE_BINARY_DIR}" "-DSLOPPY_CLI=$<TARGET_FILE:sloppy>"
                "-DSLOPPYC_EXECUTABLE=${SLOPPYC_BUILT_EXECUTABLE}" "-DSLOPPY_COMMAND=build"
                "-DSLOPPY_CASE=build-bare-import"
                "-DSLOPPY_SOURCE=compiler/tests/fixtures/unsupported-import-specifier/input.js"
                "-DSLOPPY_EXPECTED_ERROR=unsupported import specifier" -P
                "${PROJECT_SOURCE_DIR}/tests/cmake/check_source_input_run.cmake")
        set_tests_properties(sloppy.build.source_input_bare_import
                             PROPERTIES LABELS "source-input")

        add_test(
            NAME sloppy.build.source_input_node_import
            COMMAND
                "${CMAKE_COMMAND}" "-DPROJECT_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
                "-DCMAKE_BINARY_DIR=${CMAKE_BINARY_DIR}" "-DSLOPPY_CLI=$<TARGET_FILE:sloppy>"
                "-DSLOPPYC_EXECUTABLE=${SLOPPYC_BUILT_EXECUTABLE}" "-DSLOPPY_COMMAND=build"
                "-DSLOPPY_CASE=build-node-import"
                "-DSLOPPY_SOURCE=compiler/tests/fixtures/node-fs-import/input.js"
                "-DSLOPPY_EXPECTED_ERROR=unsupported import specifier" -P
                "${PROJECT_SOURCE_DIR}/tests/cmake/check_source_input_run.cmake")
        set_tests_properties(sloppy.build.source_input_node_import
                             PROPERTIES LABELS "source-input")

        add_test(
            NAME sloppy.build.source_input_dynamic_import
            COMMAND
                "${CMAKE_COMMAND}" "-DPROJECT_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
                "-DCMAKE_BINARY_DIR=${CMAKE_BINARY_DIR}" "-DSLOPPY_CLI=$<TARGET_FILE:sloppy>"
                "-DSLOPPYC_EXECUTABLE=${SLOPPYC_BUILT_EXECUTABLE}" "-DSLOPPY_COMMAND=build"
                "-DSLOPPY_CASE=build-dynamic-import"
                "-DSLOPPY_SOURCE=compiler/tests/fixtures/unsupported-dynamic-import/input.js"
                "-DSLOPPY_EXPECTED_ERROR=UNSUPPORTED_DYNAMIC_IMPORT" -P
                "${PROJECT_SOURCE_DIR}/tests/cmake/check_source_input_run.cmake")
        set_tests_properties(sloppy.build.source_input_dynamic_import
                             PROPERTIES LABELS "source-input")

        add_test(
            NAME sloppy.build.source_input_unsupported_extension
            COMMAND
                "${CMAKE_COMMAND}" "-DPROJECT_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
                "-DCMAKE_BINARY_DIR=${CMAKE_BINARY_DIR}" "-DSLOPPY_CLI=$<TARGET_FILE:sloppy>"
                "-DSLOPPYC_EXECUTABLE=${SLOPPYC_BUILT_EXECUTABLE}" "-DSLOPPY_COMMAND=build"
                "-DSLOPPY_CASE=build-unsupported-extension"
                "-DSLOPPY_SOURCE=compiler/tests/fixtures/unsupported-typescript-handler/input.tsx"
                "-DSLOPPY_EXPECTED_ERROR=unsupported source input" -P
                "${PROJECT_SOURCE_DIR}/tests/cmake/check_source_input_run.cmake")
        set_tests_properties(sloppy.build.source_input_unsupported_extension
                             PROPERTIES LABELS "source-input")

        add_test(
            NAME sloppy.run.source_input_unsupported_extension
            COMMAND
                "${CMAKE_COMMAND}" "-DPROJECT_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
                "-DCMAKE_BINARY_DIR=${CMAKE_BINARY_DIR}" "-DSLOPPY_CLI=$<TARGET_FILE:sloppy>"
                "-DSLOPPYC_EXECUTABLE=${SLOPPYC_BUILT_EXECUTABLE}"
                "-DSLOPPY_CASE=run-unsupported-extension"
                "-DSLOPPY_SOURCE=compiler/tests/fixtures/unsupported-typescript-handler/input.tsx"
                "-DSLOPPY_EXPECTED_ERROR=unsupported source input" -P
                "${PROJECT_SOURCE_DIR}/tests/cmake/check_source_input_run.cmake")
        set_tests_properties(sloppy.run.source_input_unsupported_extension
                             PROPERTIES LABELS "source-input")

        add_test(
            NAME sloppy.run.source_input_unsupported_typescript
            COMMAND
                "${CMAKE_COMMAND}" "-DPROJECT_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
                "-DCMAKE_BINARY_DIR=${CMAKE_BINARY_DIR}" "-DSLOPPY_CLI=$<TARGET_FILE:sloppy>"
                "-DSLOPPYC_EXECUTABLE=${SLOPPYC_BUILT_EXECUTABLE}"
                "-DSLOPPY_CASE=unsupported-typescript"
                "-DSLOPPY_SOURCE=compiler/tests/fixtures/unsupported-typescript-handler/input.ts"
                "-DSLOPPY_EXPECTED_ERROR=UNSUPPORTED_TYPESCRIPT_HANDLER" -P
                "${PROJECT_SOURCE_DIR}/tests/cmake/check_source_input_run.cmake")
        set_tests_properties(sloppy.run.source_input_unsupported_typescript
                             PROPERTIES LABELS "source-input")

        add_test(
            NAME sloppy.run.source_input_missing_compiler
            COMMAND
                "${CMAKE_COMMAND}" "-DPROJECT_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
                "-DCMAKE_BINARY_DIR=${CMAKE_BINARY_DIR}" "-DSLOPPY_CLI=$<TARGET_FILE:sloppy>"
                "-DSLOPPYC_EXECUTABLE=${CMAKE_BINARY_DIR}/missing-sloppyc.exe"
                "-DSLOPPY_CASE=missing-compiler"
                "-DSLOPPY_SOURCE=examples/compiler-hello/app.js"
                "-DSLOPPY_EXPECTED_ERROR=compiler unavailable" -P
                "${PROJECT_SOURCE_DIR}/tests/cmake/check_source_input_run.cmake")
        set_tests_properties(sloppy.run.source_input_missing_compiler
                             PROPERTIES LABELS "source-input")
    endif()

    add_test(
        NAME sloppy.run.tampered_route_artifact_rejected
        COMMAND
            "${CMAKE_COMMAND}" "-DPROJECT_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
            "-DCMAKE_BINARY_DIR=${CMAKE_BINARY_DIR}" "-DSLOPPY_CLI=$<TARGET_FILE:sloppy>" -P
            "${PROJECT_SOURCE_DIR}/tests/cmake/check_tampered_route_artifact.cmake")
    set_tests_properties(
        sloppy.run.tampered_route_artifact_rejected
        PROPERTIES WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}" LABELS "runtime-artifact;route;dispatch")

    if(NOT SLOPPY_ENABLE_V8)
        add_test(
            NAME sloppy.run.v8_disabled_is_clear
            COMMAND
                "${CMAKE_COMMAND}" "-DSLOPPY_CLI=$<TARGET_FILE:sloppy>"
                "-DSLOPPY_CLI_ARGS=run;--artifacts;tests/integration/execution/compiler_artifact;--once;GET;/"
                "-DSLOPPY_EXPECT_NO_STDLIB_FS=1"
                "-DSLOPPY_EXPECTED_ERROR=requires V8-enabled build" -P
                "${PROJECT_SOURCE_DIR}/tests/cmake/check_cli_failure.cmake")
        set_tests_properties(sloppy.run.v8_disabled_is_clear
                             PROPERTIES WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}")
        add_test(
            NAME sloppy.run.runtime_artifacts_without_fs_feature_non_v8
            COMMAND
                "${CMAKE_COMMAND}" "-DSLOPPY_CLI=$<TARGET_FILE:sloppy>"
                "-DSLOPPY_CLI_ARGS=run;--artifacts;tests/integration/execution/compiler_artifact;--once;GET;/"
                "-DSLOPPY_EXPECT_NO_STDLIB_FS=1"
                "-DSLOPPY_EXPECTED_ERROR=requires V8-enabled build" -P
                "${PROJECT_SOURCE_DIR}/tests/cmake/check_cli_failure.cmake")
        set_tests_properties(
            sloppy.run.runtime_artifacts_without_fs_feature_non_v8
            PROPERTIES WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
                       LABELS "runtime-artifact;filesystem-boundary")
    else()
        if(CARGO_EXECUTABLE)
            function(sloppy_add_conformance_run_once_test test_name case_name source_path method target
                     expected_output)
                add_test(
                    NAME ${test_name}
                    COMMAND
                        "${CMAKE_COMMAND}" "-DPROJECT_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
                        "-DCMAKE_BINARY_DIR=${CMAKE_BINARY_DIR}"
                        "-DCARGO_EXECUTABLE=${CARGO_EXECUTABLE}" "-DSLOPPY_CLI=$<TARGET_FILE:sloppy>"
                        "-DSLOPPY_CONFORMANCE_CASE=${case_name}" "-DSLOPPY_SOURCE=${source_path}"
                        "-DSLOPPY_ONCE_METHOD=${method}" "-DSLOPPY_ONCE_TARGET=${target}"
                        "-DSLOPPY_EXPECTED_OUTPUT=${expected_output}" -P
                        "${PROJECT_SOURCE_DIR}/tests/cmake/check_conformance_run_once.cmake")
                set_tests_properties(${test_name} PROPERTIES LABELS "conformance;v8")
            endfunction()

            sloppy_add_conformance_run_once_test(
                conformance.hello.run_once hello examples/compiler-hello/app.js GET /
                "Hello from Sloppy")
            sloppy_add_conformance_run_once_test(
                conformance.request_context.run_once request-context examples/request-context/app.js GET
                "/users/123?q=abc&q=last"
                "\"id\":\"123\".*\"q\":\"last\".*\"method\":\"GET\".*\"path\":\"/users/123\".*\"rawTarget\":\"/users/123[?]q=abc&q=last\"")
            sloppy_add_conformance_run_once_test(
                conformance.async_handler.run_once async-handler
                compiler/tests/fixtures/async-handler/input.js GET /async "\"ok\":true")
            sloppy_add_conformance_run_once_test(
                conformance.framework_typed_handler.run_once framework-typed-handler
                tests/integration/framework-typed-handler/app.ts GET /framework/42?active=true
                "\"id\":42.*\"active\":true.*\"method\":\"GET\".*\"path\":\"/framework/42\"")
            sloppy_add_conformance_run_once_test(
                conformance.framework_di_services.run_once framework-di-services
                tests/integration/framework-di-services/app.ts GET /di/42
                "\"message\":\"user-42\".*\"counter\":7.*\"stamp\":\"transient\"")
            if(SLOPPY_BUILD_COMPILER)
                add_test(
                    NAME conformance.hello.source_input_run_once
                    COMMAND
                        "${CMAKE_COMMAND}" "-DPROJECT_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
                        "-DCMAKE_BINARY_DIR=${CMAKE_BINARY_DIR}"
                        "-DSLOPPY_CLI=$<TARGET_FILE:sloppy>"
                        "-DSLOPPYC_EXECUTABLE=${SLOPPYC_BUILT_EXECUTABLE}"
                        "-DSLOPPY_CASE=hello-v8" "-DSLOPPY_SOURCE=examples/compiler-hello/app.js"
                        "-DSLOPPY_ONCE_METHOD=GET" "-DSLOPPY_ONCE_TARGET=/"
                        "-DSLOPPY_EXPECTED_OUTPUT=Hello from Sloppy"
                        "-DSLOPPY_EXPECTED_ARTIFACT_DIR=.sloppy" -P
                        "${PROJECT_SOURCE_DIR}/tests/cmake/check_source_input_run.cmake")
                set_tests_properties(conformance.hello.source_input_run_once
                                     PROPERTIES LABELS "conformance;v8;source-input")
                add_test(
                    NAME conformance.hello_minimal.source_input_run_once
                    COMMAND
                        "${CMAKE_COMMAND}" "-DPROJECT_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
                        "-DCMAKE_BINARY_DIR=${CMAKE_BINARY_DIR}"
                        "-DSLOPPY_CLI=$<TARGET_FILE:sloppy>"
                        "-DSLOPPYC_EXECUTABLE=${SLOPPYC_BUILT_EXECUTABLE}"
                        "-DSLOPPY_CASE=hello-minimal-v8"
                        "-DSLOPPY_SOURCE=examples/hello-minimal/src/main.ts"
                        "-DSLOPPY_ONCE_METHOD=GET" "-DSLOPPY_ONCE_TARGET=/hello/Ada"
                        "-DSLOPPY_EXPECTED_OUTPUT=\"hello\":\"Ada\""
                        "-DSLOPPY_EXPECTED_ARTIFACT_DIR=.sloppy" -P
                        "${PROJECT_SOURCE_DIR}/tests/cmake/check_source_input_run.cmake")
                set_tests_properties(conformance.hello_minimal.source_input_run_once
                                     PROPERTIES LABELS "conformance;v8;source-input")
                add_test(
                    NAME conformance.users_api_sqlite.source_input_run_once
                    COMMAND
                        "${CMAKE_COMMAND}" "-DPROJECT_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
                        "-DCMAKE_BINARY_DIR=${CMAKE_BINARY_DIR}"
                        "-DSLOPPY_CLI=$<TARGET_FILE:sloppy>"
                        "-DSLOPPYC_EXECUTABLE=${SLOPPYC_BUILT_EXECUTABLE}"
                        "-DSLOPPY_CASE=users-api-sqlite-v8"
                        "-DSLOPPY_SOURCE=examples/users-api-sqlite/app.js"
                        "-DSLOPPY_ONCE_METHOD=GET" "-DSLOPPY_ONCE_TARGET=/users"
                        "-DSLOPPY_EXPECTED_OUTPUT=Ada Lovelace"
                        "-DSLOPPY_EXPECTED_ARTIFACT_DIR=.sloppy" -P
                        "${PROJECT_SOURCE_DIR}/tests/cmake/check_source_input_run.cmake")
                set_tests_properties(conformance.users_api_sqlite.source_input_run_once
                                     PROPERTIES LABELS "conformance;v8;source-input;sqlite")
                add_test(
                    NAME conformance.prealpha_control_plane.source_input_run_once
                    COMMAND
                        "${CMAKE_COMMAND}" "-DPROJECT_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
                        "-DCMAKE_BINARY_DIR=${CMAKE_BINARY_DIR}"
                        "-DSLOPPY_CLI=$<TARGET_FILE:sloppy>"
                        "-DSLOPPYC_EXECUTABLE=${SLOPPYC_BUILT_EXECUTABLE}"
                        "-DSLOPPY_CASE=prealpha-control-plane-v8"
                        "-DSLOPPY_PROJECT_FIXTURE=examples/prealpha-control-plane"
                        "-DSLOPPY_ONCE_METHOD=GET" "-DSLOPPY_ONCE_TARGET=/projects?owner=runtime"
                        "-DSLOPPY_EXPECTED_OUTPUT=Compiler Platform"
                        "-DSLOPPY_EXPECTED_ARTIFACT_DIR=.sloppy"
                        "-DSLOPPY_EXPECTED_PLAN=Projects.List"
                        "-DSLOPPY_EXPECTED_SOURCE_MAP=projects.js" -P
                        "${PROJECT_SOURCE_DIR}/tests/cmake/check_source_input_run.cmake")
                set_tests_properties(
                    conformance.prealpha_control_plane.source_input_run_once
                    PROPERTIES LABELS "conformance;v8;source-input;dogfood;sqlite")
            endif()
            sloppy_add_framework_v8_example_tests()
            add_test(
                NAME conformance.users_api_sqlite.localhost_transport
                COMMAND
                    powershell -NoProfile -ExecutionPolicy Bypass -File
                    "${PROJECT_SOURCE_DIR}/tests/scripts/test_users_api_sqlite_transport.ps1"
                    -ProjectSourceDir "${PROJECT_SOURCE_DIR}" -CMakeBinaryDir "${CMAKE_BINARY_DIR}"
                    -CargoExecutable "${CARGO_EXECUTABLE}" -SloppyCli "$<TARGET_FILE:sloppy>")
            set_tests_properties(conformance.users_api_sqlite.localhost_transport
                                 PROPERTIES LABELS "conformance;v8;transport;sqlite;capability")
        endif()

        function(sloppy_add_conformance_existing_artifact_test test_name artifacts method target
                 expected_output)
            add_test(
                NAME ${test_name}
                COMMAND
                    "${CMAKE_COMMAND}" "-DPROJECT_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
                    "-DSLOPPY_CLI=$<TARGET_FILE:sloppy>" "-DSLOPPY_ARTIFACTS=${artifacts}"
                    "-DSLOPPY_ONCE_METHOD=${method}" "-DSLOPPY_ONCE_TARGET=${target}"
                    "-DSLOPPY_EXPECTED_OUTPUT=${expected_output}" -P
                    "${PROJECT_SOURCE_DIR}/tests/cmake/check_conformance_existing_artifacts.cmake")
            set_tests_properties(${test_name} PROPERTIES LABELS "conformance;v8")
        endfunction()

        sloppy_add_conformance_existing_artifact_test(
            conformance.results.invalid_descriptor tests/integration/execution/invalid_descriptor GET /
            "500 Internal Server Error")
        set_tests_properties(
            conformance.results.invalid_descriptor PROPERTIES LABELS "conformance;v8;results")
        sloppy_add_conformance_existing_artifact_test(
            conformance.sqlite.bridge tests/integration/execution/sqlite_bridge GET /sqlite "Ada")
        set_tests_properties(
            conformance.sqlite.bridge PROPERTIES LABELS "conformance;v8;sqlite;capability")
        sloppy_add_conformance_existing_artifact_test(
            conformance.sqlite.denied_capability
            tests/integration/execution/sqlite_denied_capability GET /sqlite-denied
            "500 Internal Server Error")
        set_tests_properties(
            conformance.sqlite.denied_capability
            PROPERTIES LABELS "conformance;v8;sqlite;capability")
        add_test(
            NAME conformance.postgres.bridge_live
            COMMAND
                "${CMAKE_COMMAND}" "-DPROJECT_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
                "-DSLOPPY_CLI=$<TARGET_FILE:sloppy>" -P
                "${PROJECT_SOURCE_DIR}/tests/cmake/check_postgres_bridge_live.cmake")
        set_tests_properties(
            conformance.postgres.bridge_live
            PROPERTIES
                LABELS "conformance;v8;postgres;live-provider"
                SKIP_REGULAR_EXPRESSION "SKIP: live PostgreSQL V8 bridge tests are not configured")
        add_test(
            NAME conformance.sqlserver.bridge_live
            COMMAND
                "${CMAKE_COMMAND}" "-DPROJECT_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
                "-DSLOPPY_CLI=$<TARGET_FILE:sloppy>" -P
                "${PROJECT_SOURCE_DIR}/tests/cmake/check_sqlserver_bridge_live.cmake")
        set_tests_properties(
            conformance.sqlserver.bridge_live
            PROPERTIES
                LABELS "conformance;v8;sqlserver;live-provider"
                SKIP_REGULAR_EXPRESSION
                    "SKIP: live SQL Server V8 bridge tests are not configured|SKIP: live SQL Server V8 bridge true-async path is unavailable")
        add_test(
            NAME sloppy.run.once_hello
            COMMAND "$<TARGET_FILE:sloppy>" run --artifacts
                    tests/integration/execution/compiler_artifact --once GET /
            WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}")
        set_tests_properties(sloppy.run.once_hello PROPERTIES PASS_REGULAR_EXPRESSION "Hello from Sloppy")
        add_test(
            NAME sloppy.run.once_route_miss
            COMMAND "$<TARGET_FILE:sloppy>" run --artifacts
                    tests/integration/execution/compiler_artifact --once GET /missing
            WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}")
        set_tests_properties(sloppy.run.once_route_miss PROPERTIES PASS_REGULAR_EXPRESSION "404 Not Found")
        add_test(
            NAME sloppy.run.max_routes_plan
            COMMAND
                "${CMAKE_COMMAND}" "-DPROJECT_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
                "-DSLOPPY_TEST_BINARY_DIR=${CMAKE_BINARY_DIR}"
                "-DSLOPPY_CLI=$<TARGET_FILE:sloppy>"
                "-DSLOPPY_ROUTE_COUNT=1024" "-DSLOPPY_CHECK_RUN=1" -P
                "${PROJECT_SOURCE_DIR}/tests/cmake/check_max_routes_plan.cmake")
        set_tests_properties(sloppy.run.max_routes_plan
                             PROPERTIES WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}")
        add_test(
            NAME sloppy.run.once_unsupported_method
            COMMAND "$<TARGET_FILE:sloppy>" run --artifacts
                    tests/integration/execution/compiler_artifact --once POST /
            WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}")
        set_tests_properties(sloppy.run.once_unsupported_method
                             PROPERTIES PASS_REGULAR_EXPRESSION "405 Method Not Allowed")
        add_test(
            NAME sloppy.run.once_request_context
            COMMAND "$<TARGET_FILE:sloppy>" run --artifacts
                    tests/integration/execution/request_context --once GET /users/123?q=abc
            WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}")
        set_tests_properties(
            sloppy.run.once_request_context
            PROPERTIES PASS_REGULAR_EXPRESSION
                       "\"id\":\"123\".*\"q\":\"abc\".*\"path\":\"/users/123\"")
        add_test(
            NAME sloppy.run.once_sqlite_bridge
            COMMAND "$<TARGET_FILE:sloppy>" run --artifacts
                    tests/integration/execution/sqlite_bridge --once GET /sqlite
            WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}")
        set_tests_properties(
            sloppy.run.once_sqlite_bridge
            PROPERTIES PASS_REGULAR_EXPRESSION "name.*Ada.*name.*Grace")
        add_test(
            NAME sloppy.run.once_sqlite_denied_capability
            COMMAND "$<TARGET_FILE:sloppy>" run --artifacts
                    tests/integration/execution/sqlite_denied_capability --once GET
                    /sqlite-denied
            WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}")
        set_tests_properties(
            sloppy.run.once_sqlite_denied_capability
            PROPERTIES PASS_REGULAR_EXPRESSION "500 Internal Server Error")
        add_test(
            NAME sloppy.run.once_invalid_descriptor
            COMMAND "$<TARGET_FILE:sloppy>" run --artifacts
                    tests/integration/execution/invalid_descriptor --once GET /
            WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}")
        set_tests_properties(sloppy.run.once_invalid_descriptor
                             PROPERTIES PASS_REGULAR_EXPRESSION "500 Internal Server Error")
        add_test(
            NAME sloppy.run.missing_stdlib_asset
            COMMAND
                "${CMAKE_COMMAND}" "-DSLOPPY_CLI=$<TARGET_FILE:sloppy>"
                "-DSLOPPY_CLI_ARGS=run;--artifacts;tests/integration/execution/compiler_artifact;--stdlib;tests/fixtures/run/missing;--once;GET;/"
                "-DSLOPPY_EXPECTED_ERROR=stdlib asset missing" -P
                "${PROJECT_SOURCE_DIR}/tests/cmake/check_cli_failure.cmake")
        set_tests_properties(sloppy.run.missing_stdlib_asset
                             PROPERTIES WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}")
        add_test(
            NAME sloppy.run.missing_app_module
            COMMAND
                "${CMAKE_COMMAND}" "-DSLOPPY_CLI=$<TARGET_FILE:sloppy>"
                "-DSLOPPY_CLI_ARGS=run;--artifacts;tests/fixtures/run/missing-app-module;--once;GET;/"
                "-DSLOPPY_EXPECTED_ERROR=artifact path not found" -P
                "${PROJECT_SOURCE_DIR}/tests/cmake/check_cli_failure.cmake")
        set_tests_properties(sloppy.run.missing_app_module
                             PROPERTIES WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}")
        add_test(
            NAME sloppy.run.missing_handler_registration
            COMMAND
                "${CMAKE_COMMAND}" "-DSLOPPY_CLI=$<TARGET_FILE:sloppy>"
                "-DSLOPPY_CLI_ARGS=run;--artifacts;tests/integration/execution/missing_handler;--once;GET;/"
                "-DSLOPPY_EXPECTED_ERROR=unregistered handler ID" -P
                "${PROJECT_SOURCE_DIR}/tests/cmake/check_cli_failure.cmake")
        set_tests_properties(sloppy.run.missing_handler_registration
                             PROPERTIES WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}")
        add_test(
            NAME sloppy.run.duplicate_handler_registration
            COMMAND
                "${CMAKE_COMMAND}" "-DSLOPPY_CLI=$<TARGET_FILE:sloppy>"
                "-DSLOPPY_CLI_ARGS=run;--artifacts;tests/integration/execution/duplicate_handler;--once;GET;/"
                "-DSLOPPY_EXPECTED_ERROR=duplicate handler ID" -P
                "${PROJECT_SOURCE_DIR}/tests/cmake/check_cli_failure.cmake")
        set_tests_properties(sloppy.run.duplicate_handler_registration
                             PROPERTIES WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}")
    endif()
