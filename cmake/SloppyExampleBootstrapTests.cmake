# Bootstrap asset, example API-shape, stdlib, and package tests. Included by cmake/SloppyTests.cmake.

    add_test(
        NAME bootstrap.stdlib.assets
        COMMAND
            "${CMAKE_COMMAND}" "-DSLOPPY_BOOTSTRAP_SOURCE_DIR=${SLOPPY_BOOTSTRAP_SOURCE_DIR}"
            "-DSLOPPY_BOOTSTRAP_BUILD_DIR=${SLOPPY_BOOTSTRAP_BUILD_DIR}" -P
            "${PROJECT_SOURCE_DIR}/tests/cmake/check_bootstrap_assets.cmake")
    add_test(
        NAME bootstrap.stdlib.api_shape
        COMMAND
            "${CMAKE_COMMAND}" "-DSLOPPY_BOOTSTRAP_SOURCE_DIR=${SLOPPY_BOOTSTRAP_SOURCE_DIR}"
            "-DSLOPPY_BOOTSTRAP_BUILD_DIR=${SLOPPY_BOOTSTRAP_BUILD_DIR}" -P
            "${PROJECT_SOURCE_DIR}/tests/cmake/check_bootstrap_api.cmake")
    add_test(
        NAME examples.hello.api_shape
        COMMAND "${CMAKE_COMMAND}" "-DPROJECT_SOURCE_DIR=${PROJECT_SOURCE_DIR}" -P
                "${PROJECT_SOURCE_DIR}/tests/cmake/check_hello_example.cmake")
    add_test(
        NAME examples.ergonomics.api_shape
        COMMAND "${CMAKE_COMMAND}" "-DPROJECT_SOURCE_DIR=${PROJECT_SOURCE_DIR}" -P
                "${PROJECT_SOURCE_DIR}/tests/cmake/check_ergonomics_example.cmake")
    add_test(
        NAME examples.modules_basic.api_shape
        COMMAND "${CMAKE_COMMAND}" "-DPROJECT_SOURCE_DIR=${PROJECT_SOURCE_DIR}" -P
                "${PROJECT_SOURCE_DIR}/tests/cmake/check_modules_basic_example.cmake")
    add_test(
        NAME examples.data_foundation.api_shape
        COMMAND "${CMAKE_COMMAND}" "-DPROJECT_SOURCE_DIR=${PROJECT_SOURCE_DIR}" -P
                "${PROJECT_SOURCE_DIR}/tests/cmake/check_data_foundation_example.cmake")
    add_test(
        NAME examples.sqlite_basic.api_shape
        COMMAND "${CMAKE_COMMAND}" "-DPROJECT_SOURCE_DIR=${PROJECT_SOURCE_DIR}" -P
                "${PROJECT_SOURCE_DIR}/tests/cmake/check_sqlite_basic_example.cmake")
    add_test(
        NAME examples.prealpha_control_plane.api_shape
        COMMAND "${CMAKE_COMMAND}" "-DPROJECT_SOURCE_DIR=${PROJECT_SOURCE_DIR}" -P
                "${PROJECT_SOURCE_DIR}/tests/cmake/check_prealpha_control_plane_example.cmake")
    add_test(
        NAME examples.postgres_basic.api_shape
        COMMAND "${CMAKE_COMMAND}" "-DPROJECT_SOURCE_DIR=${PROJECT_SOURCE_DIR}" -P
                "${PROJECT_SOURCE_DIR}/tests/cmake/check_postgres_basic_example.cmake")
    add_test(
        NAME examples.sqlserver_basic.api_shape
        COMMAND "${CMAKE_COMMAND}" "-DPROJECT_SOURCE_DIR=${PROJECT_SOURCE_DIR}" -P
                "${PROJECT_SOURCE_DIR}/tests/cmake/check_sqlserver_basic_example.cmake")
    add_test(
        NAME examples.request_context.api_shape
        COMMAND "${CMAKE_COMMAND}" "-DPROJECT_SOURCE_DIR=${PROJECT_SOURCE_DIR}" -P
                "${PROJECT_SOURCE_DIR}/tests/cmake/check_request_context_example.cmake")
    add_test(
        NAME examples.time.api_shape
        COMMAND "${CMAKE_COMMAND}" "-DPROJECT_SOURCE_DIR=${PROJECT_SOURCE_DIR}" -P
                "${PROJECT_SOURCE_DIR}/tests/cmake/check_time_examples.cmake")
    add_test(
        NAME examples.crypto.api_shape
        COMMAND "${CMAKE_COMMAND}" "-DPROJECT_SOURCE_DIR=${PROJECT_SOURCE_DIR}" -P
                "${PROJECT_SOURCE_DIR}/tests/cmake/check_crypto_examples.cmake")
    add_test(
        NAME examples.codec.api_shape
        COMMAND "${CMAKE_COMMAND}" "-DPROJECT_SOURCE_DIR=${PROJECT_SOURCE_DIR}" -P
                "${PROJECT_SOURCE_DIR}/tests/cmake/check_codec_examples.cmake")
    add_test(
        NAME examples.net.api_shape
        COMMAND "${CMAKE_COMMAND}" "-DPROJECT_SOURCE_DIR=${PROJECT_SOURCE_DIR}" -P
                "${PROJECT_SOURCE_DIR}/tests/cmake/check_net_examples.cmake")
    add_test(
        NAME examples.os.api_shape
        COMMAND "${CMAKE_COMMAND}" "-DPROJECT_SOURCE_DIR=${PROJECT_SOURCE_DIR}" -P
                "${PROJECT_SOURCE_DIR}/tests/cmake/check_os_examples.cmake")
    add_test(
        NAME examples.workers.api_shape
        COMMAND "${CMAKE_COMMAND}" "-DPROJECT_SOURCE_DIR=${PROJECT_SOURCE_DIR}" -P
                "${PROJECT_SOURCE_DIR}/tests/cmake/check_workers_examples.cmake")
    add_test(
        NAME examples.config.api_shape
        COMMAND "${CMAKE_COMMAND}" "-DPROJECT_SOURCE_DIR=${PROJECT_SOURCE_DIR}" -P
                "${PROJECT_SOURCE_DIR}/tests/cmake/check_config_examples.cmake")
    add_test(
        NAME examples.fs.api_shape
        COMMAND "${CMAKE_COMMAND}" "-DPROJECT_SOURCE_DIR=${PROJECT_SOURCE_DIR}" -P
                "${PROJECT_SOURCE_DIR}/tests/cmake/check_fs_examples.cmake")
    add_test(
        NAME examples.core_integration.api_shape
        COMMAND "${CMAKE_COMMAND}" "-DPROJECT_SOURCE_DIR=${PROJECT_SOURCE_DIR}" -P
                "${PROJECT_SOURCE_DIR}/tests/cmake/check_core_integration_examples.cmake")
    sloppy_add_framework_static_example_tests()
    set_tests_properties(
        examples.core_integration.api_shape PROPERTIES LABELS
                                                     "core-integration;examples;conformance")
    add_test(
        NAME bootstrap.stdlib.testservices_runtime
        COMMAND
            "${CMAKE_COMMAND}" "-DPROJECT_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
            "-DSLOPPY_CLI=$<TARGET_FILE:sloppy>"
            "-DSLOPPY_ARTIFACTS=tests/integration/execution/testservices_runtime"
            "-DSLOPPY_ONCE_METHOD=GET" "-DSLOPPY_ONCE_TARGET=/testservices"
            "-DSLOPPY_EXPECTED_OUTPUT=\"ok\":true" -P
            "${PROJECT_SOURCE_DIR}/tests/cmake/check_conformance_existing_artifacts.cmake")
    set_tests_properties(
        bootstrap.stdlib.testservices_runtime
        PROPERTIES
            LABELS "bootstrap;v8"
            SKIP_REGULAR_EXPRESSION "requires V8-enabled build")
    add_test(
        NAME bootstrap.stdlib.testservices_postgres_live
        COMMAND
            "${CMAKE_COMMAND}" "-DPROJECT_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
            "-DSLOPPY_CLI=$<TARGET_FILE:sloppy>" -P
            "${PROJECT_SOURCE_DIR}/tests/cmake/check_testservices_postgres_live.cmake")
    set_tests_properties(
        bootstrap.stdlib.testservices_postgres_live
        PROPERTIES
            LABELS "bootstrap;v8;live-provider;postgres"
            SKIP_REGULAR_EXPRESSION "SKIP: live PostgreSQL TestServices lane is not configured|requires V8-enabled build")
    add_test(
        NAME bootstrap.stdlib.testservices_sqlserver_live
        COMMAND
            "${CMAKE_COMMAND}" "-DPROJECT_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
            "-DSLOPPY_CLI=$<TARGET_FILE:sloppy>" -P
            "${PROJECT_SOURCE_DIR}/tests/cmake/check_testservices_sqlserver_live.cmake")
    set_tests_properties(
        bootstrap.stdlib.testservices_sqlserver_live
        PROPERTIES
            LABELS "bootstrap;v8;live-provider;sqlserver"
            SKIP_REGULAR_EXPRESSION "SKIP: live SQL Server TestServices lane is not configured|requires V8-enabled build")

    if(NODE_EXECUTABLE)
        add_test(
            NAME bootstrap.stdlib.import_graph
            COMMAND
                "${CMAKE_COMMAND}" -E env
                "SLOPPY_BOOTSTRAP_BUILD_DIR=${SLOPPY_BOOTSTRAP_BUILD_DIR}" "${NODE_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/tests/bootstrap/test_import_graph.mjs")
        add_test(
            NAME bootstrap.stdlib.public_exports
            COMMAND "${NODE_EXECUTABLE}"
                    "${PROJECT_SOURCE_DIR}/tests/bootstrap/test_stdlib_public_exports.mjs")
        add_test(
            NAME bootstrap.stdlib.app_host_foundation
            COMMAND "${NODE_EXECUTABLE}"
                    "${PROJECT_SOURCE_DIR}/tests/bootstrap/test_app_host_foundation.mjs")
        add_test(
            NAME bootstrap.stdlib.realtime_framework
            COMMAND "${NODE_EXECUTABLE}"
                    "${PROJECT_SOURCE_DIR}/tests/bootstrap/test_realtime_framework.mjs")
        add_test(
            NAME bootstrap.stdlib.realtime_runtime_classic
            COMMAND "${NODE_EXECUTABLE}"
                    "${PROJECT_SOURCE_DIR}/tests/bootstrap/test_realtime_runtime_classic.mjs")
        add_test(
            NAME bootstrap.stdlib.testhost_process_modes
            COMMAND
                "${CMAKE_COMMAND}" -E env "SLOPPY_TESTHOST_CLI=$<TARGET_FILE:sloppy>"
                "${NODE_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tests/bootstrap/test_testhost_process_modes.mjs")
        add_test(
            NAME bootstrap.stdlib.prealpha_control_plane_dogfood
            COMMAND "${NODE_EXECUTABLE}"
                    "${PROJECT_SOURCE_DIR}/tests/bootstrap/test_prealpha_control_plane_dogfood.mjs")
        add_test(
            NAME bootstrap.stdlib.modules
            COMMAND "${NODE_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tests/bootstrap/test_modules.mjs")
        add_test(
            NAME bootstrap.stdlib.data_foundation
            COMMAND "${NODE_EXECUTABLE}"
                    "${PROJECT_SOURCE_DIR}/tests/bootstrap/test_data_foundation.mjs")
        add_test(
            NAME bootstrap.stdlib.orm
            COMMAND "${NODE_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tests/bootstrap/test_orm.mjs")
        add_test(
            NAME bootstrap.stdlib.orm_runtime_classic
            COMMAND "${NODE_EXECUTABLE}"
                    "${PROJECT_SOURCE_DIR}/tests/bootstrap/test_orm_runtime_classic.mjs")
        add_test(
            NAME bootstrap.stdlib.orm_runtime_classic_sync
            COMMAND "${NODE_EXECUTABLE}"
                    "${PROJECT_SOURCE_DIR}/tools/scripts/sync-orm-runtime-classic.mjs" "--check")
        add_test(
            NAME bootstrap.stdlib.orm_testhost
            COMMAND "${NODE_EXECUTABLE}"
                    "${PROJECT_SOURCE_DIR}/tests/bootstrap/test_orm_testhost.mjs")
        add_test(
            NAME bootstrap.stdlib.cache_platform
            COMMAND "${NODE_EXECUTABLE}"
                    "${PROJECT_SOURCE_DIR}/tests/bootstrap/test_cache_platform.mjs")
        add_test(
            NAME bootstrap.stdlib.codec
            COMMAND "${NODE_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tests/bootstrap/test_codec.mjs")
        add_test(NAME bootstrap.stdlib.auth
                 COMMAND "${NODE_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tests/bootstrap/test_auth.mjs")
        add_test(NAME bootstrap.stdlib.ops_management
                 COMMAND "${NODE_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tests/bootstrap/test_ops_management.mjs")
        add_test(NAME bootstrap.stdlib.ops_properties
                 COMMAND "${NODE_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tests/bootstrap/test_ops_properties.mjs")
        add_test(
            NAME bootstrap.stdlib.codec_properties
            COMMAND "${NODE_EXECUTABLE}"
                    "${PROJECT_SOURCE_DIR}/tests/bootstrap/test_codec_properties.mjs")
        add_test(
            NAME bootstrap.stdlib.property
            COMMAND
                "${NODE_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tests/bootstrap/property/run_property_tests.mjs"
                "--seed" "12345" "--iterations" "256")
        add_test(
            NAME bootstrap.stdlib.os
            COMMAND "${NODE_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tests/bootstrap/test_os.mjs")
        add_test(
            NAME bootstrap.stdlib.http_client
            COMMAND "${NODE_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tests/bootstrap/test_http_client.mjs")
        add_test(
            NAME bootstrap.stdlib.webhooks
            COMMAND "${NODE_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tests/bootstrap/test_webhooks.mjs")
        add_test(
            NAME bootstrap.stdlib.webhooks_runtime_classic
            COMMAND "${NODE_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tests/bootstrap/test_webhooks_runtime_classic.mjs")
        add_test(
            NAME bootstrap.stdlib.workers
            COMMAND "${NODE_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tests/bootstrap/test_workers.mjs")
        add_test(
            NAME bootstrap.stdlib.ffi
            COMMAND "${NODE_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tests/bootstrap/test_ffi.mjs")
        add_test(
            NAME bootstrap.stdlib.core_integration
            COMMAND "${NODE_EXECUTABLE}"
                    "${PROJECT_SOURCE_DIR}/tests/bootstrap/test_core_integration.mjs")
        set_tests_properties(
            bootstrap.stdlib.import_graph bootstrap.stdlib.public_exports
            bootstrap.stdlib.app_host_foundation bootstrap.stdlib.testhost_process_modes
            bootstrap.stdlib.realtime_framework
            bootstrap.stdlib.prealpha_control_plane_dogfood
            bootstrap.stdlib.modules
            bootstrap.stdlib.data_foundation bootstrap.stdlib.orm bootstrap.stdlib.orm_runtime_classic
            bootstrap.stdlib.orm_runtime_classic_sync bootstrap.stdlib.orm_testhost
            bootstrap.stdlib.testservices_runtime bootstrap.stdlib.cache_platform bootstrap.stdlib.codec
            bootstrap.stdlib.auth
            bootstrap.stdlib.ops_management bootstrap.stdlib.ops_properties bootstrap.stdlib.property
            bootstrap.stdlib.os
            bootstrap.stdlib.http_client bootstrap.stdlib.webhooks bootstrap.stdlib.webhooks_runtime_classic
            bootstrap.stdlib.workers bootstrap.stdlib.ffi
            bootstrap.stdlib.codec_properties
            bootstrap.stdlib.core_integration PROPERTIES
            WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}")
        set_tests_properties(
            bootstrap.stdlib.core_integration PROPERTIES LABELS
                                                       "core-integration;bootstrap;conformance")
    endif()

    if(WIN32 AND SLOPPY_PACKAGE_ARCHIVE)
        add_test(
            NAME conformance.package.windows_outside_checkout
            COMMAND powershell -NoProfile -ExecutionPolicy Bypass -File
                    "${PROJECT_SOURCE_DIR}/tools/windows/test-package.ps1" -PackagePath
                    "${SLOPPY_PACKAGE_ARCHIVE}" -MetadataPath
                    "${PROJECT_SOURCE_DIR}/tests/fixtures/package/windows-default/case.json")
        set_tests_properties(
            conformance.package.windows_outside_checkout
            PROPERTIES LABELS "conformance;package;outside-checkout")
    endif()
