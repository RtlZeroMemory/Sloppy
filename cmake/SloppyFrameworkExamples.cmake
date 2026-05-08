function(sloppy_add_framework_v2_static_example_tests)
    add_test(
        NAME examples.framework_v2.api_shape
        COMMAND "${CMAKE_COMMAND}" "-DPROJECT_SOURCE_DIR=${PROJECT_SOURCE_DIR}" -P
                "${PROJECT_SOURCE_DIR}/tests/cmake/check_framework_v2_examples.cmake")
    set_tests_properties(examples.framework_v2.api_shape PROPERTIES LABELS "examples;conformance")
endfunction()

function(sloppy_add_framework_v2_compile_example_tests)
    if(NOT CARGO_EXECUTABLE OR NOT SLOPPY_BUILD_COMPILER)
        return()
    endif()

    sloppy_add_conformance_compile_test(
        conformance.framework_v2_hello.compile_artifacts framework-v2-hello
        examples/framework-v2-hello/app.ts)
    sloppy_add_conformance_compile_test(
        conformance.framework_v2_validation_errors.compile_artifacts
        framework-v2-validation-errors examples/framework-v2-validation-errors/app.ts)
    sloppy_add_conformance_compile_test(
        conformance.framework_v2_explicit_binding.compile_artifacts
        framework-v2-explicit-binding examples/framework-v2-explicit-binding/app.ts)
    sloppy_add_conformance_compile_test(
        conformance.framework_v2_di_services.compile_artifacts framework-v2-di-services
        examples/framework-v2-di-services/app.ts)
    sloppy_add_conformance_compile_test(
        conformance.framework_v2_sqlite_crud.compile_artifacts framework-v2-sqlite-crud
        examples/framework-v2-sqlite-crud/app.ts)
    sloppy_add_conformance_compile_test(
        conformance.framework_v2_postgres_crud.compile_artifacts framework-v2-postgres-crud
        examples/framework-v2-postgres-crud/app.ts)
    sloppy_add_conformance_compile_test(
        conformance.framework_v2_sqlserver_crud.compile_artifacts framework-v2-sqlserver-crud
        examples/framework-v2-sqlserver-crud/app.ts)

    sloppy_add_example_tooling_test(
        examples.framework_v2_hello.tooling framework-v2-hello
        examples/framework-v2-hello/app.ts "Hello.Get" "/hello/\\{name\\}" "" "\"findings\""
        "x-slop-completeness")
    sloppy_add_example_tooling_test(
        examples.framework_v2_validation_errors.tooling framework-v2-validation-errors
        examples/framework-v2-validation-errors/app.ts "UserCreate" "Users.Create" ""
        "\"findings\"" "UserCreate")
    sloppy_add_example_tooling_test(
        examples.framework_v2_explicit_binding.tooling framework-v2-explicit-binding
        examples/framework-v2-explicit-binding/app.ts "UserPatch" "Users.Patch" ""
        "\"findings\"" "x-slop-completeness")
    sloppy_add_example_tooling_test(
        examples.framework_v2_di_services.tooling framework-v2-di-services
        examples/framework-v2-di-services/app.ts "Di.Get" "/di/\\{id:int\\}" "" "\"findings\""
        "x-slop-completeness")
    sloppy_add_example_tooling_test(
        examples.framework_v2_sqlite_crud.tooling framework-v2-sqlite-crud
        examples/framework-v2-sqlite-crud/app.ts "data.main" "injection:main"
        "No inferred route capabilities" "\"findings\"" "x-slop-capabilities")
endfunction()

function(sloppy_add_framework_v2_v8_example_tests)
    if(NOT SLOPPY_ENABLE_V8 OR NOT CARGO_EXECUTABLE)
        return()
    endif()

    sloppy_add_conformance_run_once_test(
        conformance.framework_v2_hello.run_once framework-v2-hello
        examples/framework-v2-hello/app.ts GET /hello/Ada
        "\"hello\":\"Ada\".*\"method\":\"GET\"")
    sloppy_add_conformance_run_once_test(
        conformance.framework_v2_di_services_example.run_once framework-v2-di-services-example
        examples/framework-v2-di-services/app.ts GET /di/42
        "\"message\":\"user-42\".*\"counter\":7.*\"stamp\":\"transient\"")
    sloppy_add_conformance_run_once_test(
        conformance.framework_v2_sqlite_crud.run_once framework-v2-sqlite-crud
        examples/framework-v2-sqlite-crud/app.ts GET /users "Ada Lovelace")

    if(SLOPPY_BUILD_COMPILER)
        add_test(
            NAME conformance.framework_v2_hello.source_input_run_once
            COMMAND
                "${CMAKE_COMMAND}" "-DPROJECT_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
                "-DCMAKE_BINARY_DIR=${CMAKE_BINARY_DIR}"
                "-DSLOPPY_CLI=$<TARGET_FILE:sloppy>"
                "-DSLOPPYC_EXECUTABLE=${SLOPPYC_BUILT_EXECUTABLE}"
                "-DSLOPPY_CASE=framework-v2-hello-v8"
                "-DSLOPPY_SOURCE=examples/framework-v2-hello/app.ts"
                "-DSLOPPY_ONCE_METHOD=GET" "-DSLOPPY_ONCE_TARGET=/hello/Ada"
                "-DSLOPPY_EXPECTED_OUTPUT=\"hello\":\"Ada\""
                "-DSLOPPY_EXPECTED_ARTIFACT_DIR=.sloppy/cache/dev/source-input" -P
                "${PROJECT_SOURCE_DIR}/tests/cmake/check_source_input_run.cmake")
        set_tests_properties(
            conformance.framework_v2_hello.source_input_run_once
            PROPERTIES LABELS "conformance;v8;source-input;framework-v2")

        add_test(
            NAME conformance.framework_v2_sqlite_crud.source_input_run_once
            COMMAND
                "${CMAKE_COMMAND}" "-DPROJECT_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
                "-DCMAKE_BINARY_DIR=${CMAKE_BINARY_DIR}"
                "-DSLOPPY_CLI=$<TARGET_FILE:sloppy>"
                "-DSLOPPYC_EXECUTABLE=${SLOPPYC_BUILT_EXECUTABLE}"
                "-DSLOPPY_CASE=framework-v2-sqlite-crud-v8"
                "-DSLOPPY_SOURCE=examples/framework-v2-sqlite-crud/app.ts"
                "-DSLOPPY_ONCE_METHOD=GET" "-DSLOPPY_ONCE_TARGET=/users"
                "-DSLOPPY_EXPECTED_OUTPUT=Ada Lovelace"
                "-DSLOPPY_EXPECTED_ARTIFACT_DIR=.sloppy/cache/dev/source-input" -P
                "${PROJECT_SOURCE_DIR}/tests/cmake/check_source_input_run.cmake")
        set_tests_properties(
            conformance.framework_v2_sqlite_crud.source_input_run_once
            PROPERTIES LABELS "conformance;v8;source-input;framework-v2;sqlite")
    endif()
endfunction()
