# CLI golden-output and CLI documentation tests. Included by cmake/SloppyTests.cmake.

    function(sloppy_add_cli_golden_test test_name expected_file)
        add_test(
            NAME ${test_name}
            COMMAND
                "${CMAKE_COMMAND}" "-DSLOPPY_CLI=$<TARGET_FILE:sloppy>"
                "-DSLOPPY_EXPECTED=${PROJECT_SOURCE_DIR}/${expected_file}"
                "-DSLOPPY_CLI_ARGS=${ARGN}" -P
                "${PROJECT_SOURCE_DIR}/tests/cmake/check_cli_output.cmake")
        set_tests_properties(${test_name} PROPERTIES WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}")
    endfunction()

    function(sloppy_add_cli_nonzero_golden_test test_name expected_file)
        add_test(
            NAME ${test_name}
            COMMAND
                "${CMAKE_COMMAND}" "-DSLOPPY_CLI=$<TARGET_FILE:sloppy>"
                "-DSLOPPY_EXPECTED=${PROJECT_SOURCE_DIR}/${expected_file}"
                "-DSLOPPY_EXPECT_NONZERO=1" "-DSLOPPY_CLI_ARGS=${ARGN}" -P
                "${PROJECT_SOURCE_DIR}/tests/cmake/check_cli_output.cmake")
        set_tests_properties(${test_name} PROPERTIES WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}")
    endfunction()

    function(sloppy_add_cli_nonzero_stderr_test test_name expected_stderr forbidden_stderr env_name
             env_value)
        add_test(
            NAME ${test_name}
            COMMAND
                "${CMAKE_COMMAND}" "-DSLOPPY_CLI=$<TARGET_FILE:sloppy>"
                "-DSLOPPY_EXPECTED_STDERR=${expected_stderr}"
                "-DSLOPPY_FORBIDDEN_STDERR=${forbidden_stderr}" "-DSLOPPY_ENV_NAME=${env_name}"
                "-DSLOPPY_ENV_VALUE=${env_value}" "-DSLOPPY_CLI_ARGS=${ARGN}" -P
                "${PROJECT_SOURCE_DIR}/tests/cmake/check_cli_nonzero_stderr_contains.cmake")
        set_tests_properties(${test_name} PROPERTIES WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}")
    endfunction()

    sloppy_add_cli_golden_test(
        sloppy.cli.routes_text tests/golden/cli/routes-text.txt routes --plan
        tests/fixtures/cli/route-metadata.plan.json --format text)
    sloppy_add_cli_golden_test(
        sloppy.cli.routes_json tests/golden/cli/routes-json.json routes --plan
        tests/fixtures/cli/route-metadata.plan.json --format json)
    sloppy_add_cli_golden_test(
        sloppy.cli.routes_compiler_text tests/golden/cli/routes-compiler-text.txt routes --plan
        compiler/tests/fixtures/grouped-route/expected/app.plan.json --format text)
    sloppy_add_cli_golden_test(
        sloppy.cli.routes_compiler_json tests/golden/cli/routes-compiler-json.json routes --plan
        compiler/tests/fixtures/grouped-route/expected/app.plan.json --format json)
    sloppy_add_cli_golden_test(
        sloppy.cli.routes_dispatch_text tests/golden/cli/routes-dispatch-text.txt routes --plan
        compiler/tests/fixtures/grouped-route/expected/app.plan.json --format text --dispatch)
    sloppy_add_cli_golden_test(
        sloppy.cli.routes_dispatch_json tests/golden/cli/routes-dispatch-json.json routes --plan
        compiler/tests/fixtures/grouped-route/expected/app.plan.json --format json --dispatch)
    sloppy_add_cli_golden_test(
        sloppy.cli.capabilities_users_text tests/golden/cli/capabilities-users-text.txt
        capabilities --plan compiler/tests/fixtures/realistic-users-api/expected/app.plan.json
        --format text)
    sloppy_add_cli_golden_test(
        sloppy.cli.capabilities_users_json tests/golden/cli/capabilities-users-json.json
        capabilities --plan compiler/tests/fixtures/realistic-users-api/expected/app.plan.json
        --format json)
    sloppy_add_cli_golden_test(
        sloppy.cli.deps_text tests/golden/cli/deps-text.txt deps --plan
        tests/fixtures/cli/dependency-graph.plan.json --format text)
    sloppy_add_cli_golden_test(
        sloppy.cli.deps_json tests/golden/cli/deps-json.json deps --plan
        tests/fixtures/cli/dependency-graph.plan.json --format json)
    sloppy_add_cli_golden_test(
        sloppy.cli.capabilities_ffi_text tests/golden/cli/capabilities-ffi-text.txt
        capabilities --plan tests/fixtures/cli/ffi-policy.plan.json --format text)
    sloppy_add_cli_golden_test(
        sloppy.cli.capabilities_ffi_json tests/golden/cli/capabilities-ffi-json.json
        capabilities --plan tests/fixtures/cli/ffi-policy.plan.json --format json)
    set(SLOPPY_DOCTOR_TEXT_GOLDEN tests/golden/cli/doctor-text.txt)
    set(SLOPPY_DOCTOR_JSON_GOLDEN tests/golden/cli/doctor-json.json)
    set(SLOPPY_DOCTOR_FFI_TEXT_GOLDEN tests/golden/cli/doctor-ffi-text.txt)
    set(SLOPPY_DOCTOR_FFI_JSON_GOLDEN tests/golden/cli/doctor-ffi-json.json)
    set(SLOPPY_DOCTOR_PARTIAL_TEXT_GOLDEN tests/golden/cli/doctor-partial-text.txt)
    set(SLOPPY_DOCTOR_PARTIAL_JSON_GOLDEN tests/golden/cli/doctor-partial-json.json)
    set(SLOPPY_DOCTOR_FS_TEXT_GOLDEN tests/golden/cli/doctor-filesystem-text.txt)
    set(SLOPPY_DOCTOR_FS_JSON_GOLDEN tests/golden/cli/doctor-filesystem-json.json)
    set(SLOPPY_DOCTOR_NET_TEXT_GOLDEN tests/golden/cli/doctor-network-text.txt)
    set(SLOPPY_DOCTOR_NET_JSON_GOLDEN tests/golden/cli/doctor-network-json.json)
    set(SLOPPY_DOCTOR_OS_TEXT_GOLDEN tests/golden/cli/doctor-os-text.txt)
    set(SLOPPY_DOCTOR_OS_JSON_GOLDEN tests/golden/cli/doctor-os-json.json)
    set(SLOPPY_DOCTOR_HTTP_CLIENT_TEXT_GOLDEN tests/golden/cli/doctor-http-client-text.txt)
    set(SLOPPY_DOCTOR_HTTP_CLIENT_JSON_GOLDEN tests/golden/cli/doctor-http-client-json.json)
    set(SLOPPY_DOCTOR_WORKERS_TEXT_GOLDEN tests/golden/cli/doctor-workers-text.txt)
    set(SLOPPY_DOCTOR_WORKERS_JSON_GOLDEN tests/golden/cli/doctor-workers-json.json)
    set(SLOPPY_DOCTOR_CONFIG_TEXT_GOLDEN tests/golden/cli/doctor-config-text.txt)
    set(SLOPPY_DOCTOR_CONFIG_JSON_GOLDEN tests/golden/cli/doctor-config-json.json)
    set(SLOPPY_DOCTOR_CRYPTO_NONCRYPTO_TEXT_GOLDEN
        tests/golden/cli/doctor-crypto-noncrypto-hash-text.txt)
    set(SLOPPY_DOCTOR_CRYPTO_NONCRYPTO_JSON_GOLDEN
        tests/golden/cli/doctor-crypto-noncrypto-hash-json.json)
    set(SLOPPY_DOCTOR_CODEC_CHECKSUM_TEXT_GOLDEN
        tests/golden/cli/doctor-codec-checksum-text.txt)
    set(SLOPPY_DOCTOR_CODEC_CHECKSUM_JSON_GOLDEN
        tests/golden/cli/doctor-codec-checksum-json.json)
    if(SLOPPY_ENABLE_V8)
        set(SLOPPY_DOCTOR_TEXT_GOLDEN tests/golden/cli/doctor-text-v8.txt)
        set(SLOPPY_DOCTOR_JSON_GOLDEN tests/golden/cli/doctor-json-v8.json)
        set(SLOPPY_DOCTOR_FFI_TEXT_GOLDEN tests/golden/cli/doctor-ffi-text-v8.txt)
        set(SLOPPY_DOCTOR_FFI_JSON_GOLDEN tests/golden/cli/doctor-ffi-json-v8.json)
        set(SLOPPY_DOCTOR_PARTIAL_TEXT_GOLDEN tests/golden/cli/doctor-partial-text-v8.txt)
        set(SLOPPY_DOCTOR_PARTIAL_JSON_GOLDEN tests/golden/cli/doctor-partial-json-v8.json)
        set(SLOPPY_DOCTOR_FS_TEXT_GOLDEN tests/golden/cli/doctor-filesystem-text-v8.txt)
        set(SLOPPY_DOCTOR_FS_JSON_GOLDEN tests/golden/cli/doctor-filesystem-json-v8.json)
        set(SLOPPY_DOCTOR_NET_TEXT_GOLDEN tests/golden/cli/doctor-network-text-v8.txt)
        set(SLOPPY_DOCTOR_NET_JSON_GOLDEN tests/golden/cli/doctor-network-json-v8.json)
        set(SLOPPY_DOCTOR_OS_TEXT_GOLDEN tests/golden/cli/doctor-os-text-v8.txt)
        set(SLOPPY_DOCTOR_OS_JSON_GOLDEN tests/golden/cli/doctor-os-json-v8.json)
        set(SLOPPY_DOCTOR_HTTP_CLIENT_TEXT_GOLDEN
            tests/golden/cli/doctor-http-client-text-v8.txt)
        set(SLOPPY_DOCTOR_HTTP_CLIENT_JSON_GOLDEN
            tests/golden/cli/doctor-http-client-json-v8.json)
        set(SLOPPY_DOCTOR_WORKERS_TEXT_GOLDEN tests/golden/cli/doctor-workers-text-v8.txt)
        set(SLOPPY_DOCTOR_WORKERS_JSON_GOLDEN tests/golden/cli/doctor-workers-json-v8.json)
        set(SLOPPY_DOCTOR_CONFIG_TEXT_GOLDEN tests/golden/cli/doctor-config-text-v8.txt)
        set(SLOPPY_DOCTOR_CONFIG_JSON_GOLDEN tests/golden/cli/doctor-config-json-v8.json)
        set(SLOPPY_DOCTOR_CRYPTO_NONCRYPTO_TEXT_GOLDEN
            tests/golden/cli/doctor-crypto-noncrypto-hash-text-v8.txt)
        set(SLOPPY_DOCTOR_CRYPTO_NONCRYPTO_JSON_GOLDEN
            tests/golden/cli/doctor-crypto-noncrypto-hash-json-v8.json)
        set(SLOPPY_DOCTOR_CODEC_CHECKSUM_TEXT_GOLDEN
            tests/golden/cli/doctor-codec-checksum-text-v8.txt)
        set(SLOPPY_DOCTOR_CODEC_CHECKSUM_JSON_GOLDEN
            tests/golden/cli/doctor-codec-checksum-json-v8.json)
    endif()
    sloppy_add_cli_golden_test(
        sloppy.cli.doctor_text ${SLOPPY_DOCTOR_TEXT_GOLDEN} doctor --plan
        tests/fixtures/cli/doctor.plan.json --format text)
    sloppy_add_cli_golden_test(
        sloppy.cli.doctor_json ${SLOPPY_DOCTOR_JSON_GOLDEN} doctor --plan
        tests/fixtures/cli/doctor.plan.json --format json)
    sloppy_add_cli_golden_test(
        sloppy.cli.doctor_dispatch_text tests/golden/cli/doctor-dispatch-text.txt doctor --plan
        compiler/tests/fixtures/grouped-route/expected/app.plan.json --format text --dispatch)
    sloppy_add_cli_golden_test(
        sloppy.cli.doctor_dispatch_json tests/golden/cli/doctor-dispatch-json.json doctor --plan
        compiler/tests/fixtures/grouped-route/expected/app.plan.json --format json --dispatch)
    sloppy_add_cli_golden_test(
        sloppy.cli.doctor_ffi_text ${SLOPPY_DOCTOR_FFI_TEXT_GOLDEN} doctor --plan
        tests/fixtures/cli/ffi-policy.plan.json --format text)
    sloppy_add_cli_golden_test(
        sloppy.cli.doctor_ffi_json ${SLOPPY_DOCTOR_FFI_JSON_GOLDEN} doctor --plan
        tests/fixtures/cli/ffi-policy.plan.json --format json)
    sloppy_add_cli_golden_test(
        sloppy.cli.doctor_partial_text ${SLOPPY_DOCTOR_PARTIAL_TEXT_GOLDEN} doctor --plan
        compiler/tests/fixtures/partial-body-without-schema/expected/app.plan.json --format text)
    sloppy_add_cli_golden_test(
        sloppy.cli.doctor_partial_json ${SLOPPY_DOCTOR_PARTIAL_JSON_GOLDEN} doctor --plan
        compiler/tests/fixtures/partial-body-without-schema/expected/app.plan.json --format json)
    sloppy_add_cli_golden_test(
        sloppy.cli.doctor_filesystem_text ${SLOPPY_DOCTOR_FS_TEXT_GOLDEN} doctor --plan
        tests/fixtures/cli/filesystem-policy.plan.json --format text)
    sloppy_add_cli_golden_test(
        sloppy.cli.doctor_filesystem_json ${SLOPPY_DOCTOR_FS_JSON_GOLDEN} doctor --plan
        tests/fixtures/cli/filesystem-policy.plan.json --format json)
    sloppy_add_cli_golden_test(
        sloppy.cli.doctor_network_text ${SLOPPY_DOCTOR_NET_TEXT_GOLDEN} doctor --plan
        tests/fixtures/cli/network-policy.plan.json --format text)
    sloppy_add_cli_golden_test(
        sloppy.cli.doctor_network_json ${SLOPPY_DOCTOR_NET_JSON_GOLDEN} doctor --plan
        tests/fixtures/cli/network-policy.plan.json --format json)
    sloppy_add_cli_golden_test(
        sloppy.cli.doctor_os_text ${SLOPPY_DOCTOR_OS_TEXT_GOLDEN} doctor --plan
        tests/fixtures/cli/os-policy.plan.json --format text)
    sloppy_add_cli_golden_test(
        sloppy.cli.doctor_os_json ${SLOPPY_DOCTOR_OS_JSON_GOLDEN} doctor --plan
        tests/fixtures/cli/os-policy.plan.json --format json)
    sloppy_add_cli_golden_test(
        sloppy.cli.doctor_http_client_text ${SLOPPY_DOCTOR_HTTP_CLIENT_TEXT_GOLDEN} doctor
        --plan tests/fixtures/cli/http-client-policy.plan.json --format text)
    sloppy_add_cli_golden_test(
        sloppy.cli.doctor_http_client_json ${SLOPPY_DOCTOR_HTTP_CLIENT_JSON_GOLDEN} doctor
        --plan tests/fixtures/cli/http-client-policy.plan.json --format json)
    sloppy_add_cli_golden_test(
        sloppy.cli.doctor_workers_text ${SLOPPY_DOCTOR_WORKERS_TEXT_GOLDEN} doctor --plan
        tests/fixtures/cli/workers-policy.plan.json --format text)
    sloppy_add_cli_golden_test(
        sloppy.cli.doctor_workers_json ${SLOPPY_DOCTOR_WORKERS_JSON_GOLDEN} doctor --plan
        tests/fixtures/cli/workers-policy.plan.json --format json)
    sloppy_add_cli_golden_test(
        sloppy.cli.doctor_config_text ${SLOPPY_DOCTOR_CONFIG_TEXT_GOLDEN} doctor --plan
        tests/fixtures/cli/config.plan.json --format text)
    sloppy_add_cli_golden_test(
        sloppy.cli.doctor_config_json ${SLOPPY_DOCTOR_CONFIG_JSON_GOLDEN} doctor --plan
        tests/fixtures/cli/config.plan.json --format json)
    sloppy_add_cli_golden_test(
        sloppy.cli.doctor_crypto_noncrypto_hash_text
        ${SLOPPY_DOCTOR_CRYPTO_NONCRYPTO_TEXT_GOLDEN} doctor --plan
        tests/fixtures/cli/crypto-noncrypto-hash.plan.json --format text)
    sloppy_add_cli_golden_test(
        sloppy.cli.doctor_crypto_noncrypto_hash_json
        ${SLOPPY_DOCTOR_CRYPTO_NONCRYPTO_JSON_GOLDEN} doctor --plan
        tests/fixtures/cli/crypto-noncrypto-hash.plan.json --format json)
    sloppy_add_cli_golden_test(
        sloppy.cli.doctor_codec_checksum_text
        ${SLOPPY_DOCTOR_CODEC_CHECKSUM_TEXT_GOLDEN} doctor --plan
        tests/fixtures/cli/codec-checksum.plan.json --format text)
    sloppy_add_cli_golden_test(
        sloppy.cli.doctor_codec_checksum_json
        ${SLOPPY_DOCTOR_CODEC_CHECKSUM_JSON_GOLDEN} doctor --plan
        tests/fixtures/cli/codec-checksum.plan.json --format json)
    sloppy_add_cli_nonzero_golden_test(
        sloppy.cli.audit_text tests/golden/cli/audit-text.txt audit --plan
        tests/fixtures/cli/audit-problems.plan.json --format text)
    sloppy_add_cli_nonzero_golden_test(
        sloppy.cli.audit_json tests/golden/cli/audit-json.json audit --plan
        tests/fixtures/cli/audit-problems.plan.json --format json)
    sloppy_add_cli_nonzero_golden_test(
        sloppy.cli.audit_config_text tests/golden/cli/audit-config-text.txt audit --plan
        tests/fixtures/cli/config.plan.json --format text)
    sloppy_add_cli_nonzero_golden_test(
        sloppy.cli.audit_config_json tests/golden/cli/audit-config-json.json audit --plan
        tests/fixtures/cli/config.plan.json --format json)
    sloppy_add_cli_nonzero_stderr_test(
        sloppy.cli.db_postgres_optional_dependency
        "PostgreSQL provider is unavailable(.|\n|\r)*This only matters for apps that use PostgreSQL(.|\n|\r)*optional PostgreSQL provider package"
        "pg-secret-leak" "Sloppy__Providers__postgres__main__connectionString"
        "postgres://user:pg-secret-leak@127.0.0.1:1/sloppy?connect_timeout=1" db status
        tests/fixtures/cli/db-postgres-missing/compiled --provider main)
    sloppy_add_cli_nonzero_stderr_test(
        sloppy.cli.db_sqlserver_optional_dependency
        "SQL Server provider is unavailable(.|\n|\r)*This only matters for apps that use SQL Server(.|\n|\r)*Microsoft ODBC Driver 17 or 18"
        "sql-secret-leak" "Sloppy__Providers__sqlserver__main__connectionString"
        "Driver={Sloppy Missing Driver For Tests}\\;PWD=sql-secret-leak" db status
        tests/fixtures/cli/db-sqlserver-missing/compiled --provider main)
    sloppy_add_cli_golden_test(
        sloppy.cli.audit_partial_json tests/golden/cli/audit-partial-json.json audit --plan
        compiler/tests/fixtures/partial-body-without-schema/expected/app.plan.json --format json)
    sloppy_add_cli_golden_test(
        sloppy.cli.audit_clean_text tests/golden/cli/audit-clean-text.txt audit --plan
        tests/fixtures/cli/route-metadata.plan.json --format text)
    sloppy_add_cli_golden_test(
        sloppy.cli.audit_clean_json tests/golden/cli/audit-clean-json.json audit --plan
        tests/fixtures/cli/route-metadata.plan.json --format json)
    sloppy_add_cli_golden_test(
        sloppy.cli.audit_filesystem_text tests/golden/cli/audit-filesystem-text.txt audit --plan
        tests/fixtures/cli/filesystem-policy.plan.json --format text)
    sloppy_add_cli_golden_test(
        sloppy.cli.audit_filesystem_json tests/golden/cli/audit-filesystem-json.json audit --plan
        tests/fixtures/cli/filesystem-policy.plan.json --format json)
    sloppy_add_cli_golden_test(
        sloppy.cli.audit_network_text tests/golden/cli/audit-network-text.txt audit --plan
        tests/fixtures/cli/network-policy.plan.json --format text)
    sloppy_add_cli_golden_test(
        sloppy.cli.audit_network_json tests/golden/cli/audit-network-json.json audit --plan
        tests/fixtures/cli/network-policy.plan.json --format json)
    sloppy_add_cli_golden_test(
        sloppy.cli.audit_os_text tests/golden/cli/audit-os-text.txt audit --plan
        tests/fixtures/cli/os-policy.plan.json --format text)
    sloppy_add_cli_golden_test(
        sloppy.cli.audit_os_json tests/golden/cli/audit-os-json.json audit --plan
        tests/fixtures/cli/os-policy.plan.json --format json)
    sloppy_add_cli_golden_test(
        sloppy.cli.audit_http_client_text tests/golden/cli/audit-http-client-text.txt audit
        --plan tests/fixtures/cli/http-client-policy.plan.json --format text)
    sloppy_add_cli_golden_test(
        sloppy.cli.audit_http_client_json tests/golden/cli/audit-http-client-json.json audit
        --plan tests/fixtures/cli/http-client-policy.plan.json --format json)
    sloppy_add_cli_golden_test(
        sloppy.cli.audit_workers_text tests/golden/cli/audit-workers-text.txt audit --plan
        tests/fixtures/cli/workers-policy.plan.json --format text)
    sloppy_add_cli_golden_test(
        sloppy.cli.audit_workers_json tests/golden/cli/audit-workers-json.json audit --plan
        tests/fixtures/cli/workers-policy.plan.json --format json)
    sloppy_add_cli_golden_test(
        sloppy.cli.audit_ffi_text tests/golden/cli/audit-ffi-text.txt audit --plan
        tests/fixtures/cli/ffi-policy.plan.json --format text)
    sloppy_add_cli_golden_test(
        sloppy.cli.audit_ffi_json tests/golden/cli/audit-ffi-json.json audit --plan
        tests/fixtures/cli/ffi-policy.plan.json --format json)
    sloppy_add_cli_golden_test(
        sloppy.cli.openapi_json tests/golden/cli/openapi.json openapi --plan
        tests/fixtures/cli/route-metadata.plan.json)
    sloppy_add_cli_golden_test(
        sloppy.cli.openapi_users_json tests/golden/cli/openapi-users.json openapi --plan
        compiler/tests/fixtures/realistic-users-api/expected/app.plan.json)
    sloppy_add_cli_golden_test(
        sloppy.cli.openapi_partial_json tests/golden/cli/openapi-partial.json openapi --plan
        compiler/tests/fixtures/partial-body-without-schema/expected/app.plan.json)
    sloppy_add_cli_golden_test(
        sloppy.cli.openapi_auth_missing_schemes_json
        tests/golden/cli/openapi-auth-missing-schemes.json openapi --plan
        tests/fixtures/cli/openapi-auth-missing-schemes.plan.json)
    sloppy_add_cli_golden_test(
        sloppy.cli.openapi_scalar_schema_json tests/golden/cli/openapi-scalar-schema.json openapi
        --plan tests/fixtures/cli/openapi-scalar-schema.plan.json)
    sloppy_add_cli_golden_test(
        sloppy.cli.openapi_missing_schema_json tests/golden/cli/openapi-missing-schema.json openapi
        --plan tests/fixtures/cli/openapi-missing-schema.plan.json)
    sloppy_add_cli_golden_test(
        sloppy.cli.openapi_response_content_json tests/golden/cli/openapi-response-content.json
        openapi --plan tests/fixtures/cli/openapi-response-content.plan.json)

    add_test(
        NAME sloppy.cli.openapi_too_many_route_tags
        COMMAND
            "${CMAKE_COMMAND}" "-DSLOPPY_CLI=$<TARGET_FILE:sloppy>"
            "-DSLOPPY_CLI_ARGS=openapi;--plan;tests/fixtures/cli/too-many-route-tags.plan.json"
            "-DSLOPPY_EXPECTED_ERROR=too many route tags in metadata" -P
            "${PROJECT_SOURCE_DIR}/tests/cmake/check_cli_failure.cmake")
    set_tests_properties(sloppy.cli.openapi_too_many_route_tags
                         PROPERTIES WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}")

    add_test(NAME sloppy.cli.help_includes_run COMMAND "$<TARGET_FILE:sloppy>" --help)
    set_tests_properties(sloppy.cli.help_includes_run PROPERTIES PASS_REGULAR_EXPRESSION "sloppy run")

    add_test(NAME sloppy.cli.help_includes_create COMMAND "$<TARGET_FILE:sloppy>" --help)
    set_tests_properties(sloppy.cli.help_includes_create
                         PROPERTIES PASS_REGULAR_EXPRESSION "sloppy create")

    add_test(NAME sloppy.cli.help_includes_dev COMMAND "$<TARGET_FILE:sloppy>" --help)
    set_tests_properties(sloppy.cli.help_includes_dev PROPERTIES PASS_REGULAR_EXPRESSION "sloppy dev")

    if(CARGO_EXECUTABLE AND SLOPPY_BUILD_COMPILER)
        add_test(
            NAME sloppy.cli.create_package_command
            COMMAND
                "${CMAKE_COMMAND}" "-DPROJECT_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
                "-DCMAKE_BINARY_DIR=${CMAKE_BINARY_DIR}" "-DSLOPPY_CLI=$<TARGET_FILE:sloppy>"
                "-DSLOPPYC_EXECUTABLE=${SLOPPYC_BUILT_EXECUTABLE}"
                "-DSLOPPY_ENABLE_V8=$<BOOL:${SLOPPY_ENABLE_V8}>" -P
                "${PROJECT_SOURCE_DIR}/tests/cmake/check_create_package_command.cmake")
        set_tests_properties(sloppy.cli.create_package_command PROPERTIES LABELS "cli;source-input")
    endif()

    add_test(
        NAME docs.main_contract
        COMMAND
            "${CMAKE_COMMAND}" "-DPROJECT_SOURCE_DIR=${PROJECT_SOURCE_DIR}" -P
            "${PROJECT_SOURCE_DIR}/tests/cmake/check_main_contract_docs.cmake")
    add_test(
        NAME docs.engine19_conformance_matrix
        COMMAND
            "${CMAKE_COMMAND}" "-DPROJECT_SOURCE_DIR=${PROJECT_SOURCE_DIR}" -P
            "${PROJECT_SOURCE_DIR}/tests/cmake/check_conformance_matrix_docs.cmake")
