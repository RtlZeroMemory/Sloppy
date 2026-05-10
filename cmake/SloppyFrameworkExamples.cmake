function(sloppy_add_framework_static_example_tests)
    add_test(
        NAME examples.framework.api_shape
        COMMAND "${CMAKE_COMMAND}" "-DPROJECT_SOURCE_DIR=${PROJECT_SOURCE_DIR}" -P
                "${PROJECT_SOURCE_DIR}/tests/cmake/check_framework_examples.cmake")
    set_tests_properties(examples.framework.api_shape PROPERTIES LABELS "examples;conformance")
endfunction()

function(sloppy_add_framework_compile_example_tests)
    if(NOT CARGO_EXECUTABLE OR NOT SLOPPY_BUILD_COMPILER)
        return()
    endif()

    sloppy_add_conformance_compile_test(
        conformance.framework_hello.compile_artifacts framework-hello
        examples/framework-hello/app.ts)
    sloppy_add_conformance_compile_test(
        conformance.framework_validation_errors.compile_artifacts
        framework-validation-errors examples/framework-validation-errors/app.ts)
    sloppy_add_conformance_compile_test(
        conformance.framework_explicit_binding.compile_artifacts
        framework-explicit-binding examples/framework-explicit-binding/app.ts)
    sloppy_add_conformance_compile_test(
        conformance.framework_di_services.compile_artifacts framework-di-services
        examples/framework-di-services/app.ts)
    sloppy_add_conformance_compile_test(
        conformance.framework_sqlite_crud.compile_artifacts framework-sqlite-crud
        examples/framework-sqlite-crud/app.ts)
    sloppy_add_conformance_compile_test(
        conformance.framework_postgres_crud.compile_artifacts framework-postgres-crud
        examples/framework-postgres-crud/app.ts)
    sloppy_add_conformance_compile_test(
        conformance.framework_sqlserver_crud.compile_artifacts framework-sqlserver-crud
        examples/framework-sqlserver-crud/app.ts)

    sloppy_add_example_tooling_test(
        examples.framework_hello.tooling framework-hello
        examples/framework-hello/app.ts "Hello.Get" "/hello/\\{name\\}" "" "\"findings\""
        "x-slop-completeness")
    sloppy_add_example_tooling_test(
        examples.framework_validation_errors.tooling framework-validation-errors
        examples/framework-validation-errors/app.ts "UserCreate" "Users.Create" ""
        "\"findings\"" "UserCreate")
    sloppy_add_example_tooling_test(
        examples.framework_explicit_binding.tooling framework-explicit-binding
        examples/framework-explicit-binding/app.ts "UserPatch" "Users.Patch" ""
        "\"findings\"" "x-slop-completeness")
    sloppy_add_example_tooling_test(
        examples.framework_di_services.tooling framework-di-services
        examples/framework-di-services/app.ts "Di.Get" "/di/\\{id:int\\}" "" "\"findings\""
        "x-slop-completeness")
    sloppy_add_example_tooling_test(
        examples.framework_sqlite_crud.tooling framework-sqlite-crud
        examples/framework-sqlite-crud/app.ts "data.main" "injection:main"
        "No inferred route capabilities" "\"findings\"" "x-slop-capabilities")
endfunction()

function(sloppy_add_framework_v8_example_tests)
    if(NOT SLOPPY_ENABLE_V8 OR NOT CARGO_EXECUTABLE)
        return()
    endif()

    sloppy_add_conformance_run_once_test(
        conformance.framework_hello.run_once framework-hello
        examples/framework-hello/app.ts GET /hello/Ada
        "\"hello\":\"Ada\".*\"method\":\"GET\"")
    sloppy_add_conformance_run_once_test(
        conformance.framework_di_services_example.run_once framework-di-services-example
        examples/framework-di-services/app.ts GET /di/42
        "\"message\":\"user-42\".*\"counter\":7.*\"stamp\":\"transient\"")
    sloppy_add_conformance_run_once_test(
        conformance.framework_sqlite_crud.run_once framework-sqlite-crud
        examples/framework-sqlite-crud/app.ts GET /users "Ada Lovelace")

    if(SLOPPY_BUILD_COMPILER)
        add_test(
            NAME conformance.framework_hello.source_input_run_once
            COMMAND
                "${CMAKE_COMMAND}" "-DPROJECT_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
                "-DCMAKE_BINARY_DIR=${CMAKE_BINARY_DIR}"
                "-DSLOPPY_CLI=$<TARGET_FILE:sloppy>"
                "-DSLOPPYC_EXECUTABLE=${SLOPPYC_BUILT_EXECUTABLE}"
                "-DSLOPPY_CASE=framework-hello-v8"
                "-DSLOPPY_SOURCE=examples/framework-hello/app.ts"
                "-DSLOPPY_ONCE_METHOD=GET" "-DSLOPPY_ONCE_TARGET=/hello/Ada"
                "-DSLOPPY_EXPECTED_OUTPUT=\"hello\":\"Ada\""
                "-DSLOPPY_EXPECTED_ARTIFACT_DIR=.sloppy/cache/dev/source-input" -P
                "${PROJECT_SOURCE_DIR}/tests/cmake/check_source_input_run.cmake")
        set_tests_properties(
            conformance.framework_hello.source_input_run_once
            PROPERTIES LABELS "conformance;v8;source-input;framework")

        add_test(
            NAME conformance.framework_sqlite_crud.source_input_run_once
            COMMAND
                "${CMAKE_COMMAND}" "-DPROJECT_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
                "-DCMAKE_BINARY_DIR=${CMAKE_BINARY_DIR}"
                "-DSLOPPY_CLI=$<TARGET_FILE:sloppy>"
                "-DSLOPPYC_EXECUTABLE=${SLOPPYC_BUILT_EXECUTABLE}"
                "-DSLOPPY_CASE=framework-sqlite-crud-v8"
                "-DSLOPPY_SOURCE=examples/framework-sqlite-crud/app.ts"
                "-DSLOPPY_ONCE_METHOD=GET" "-DSLOPPY_ONCE_TARGET=/users"
                "-DSLOPPY_EXPECTED_OUTPUT=Ada Lovelace"
                "-DSLOPPY_EXPECTED_ARTIFACT_DIR=.sloppy/cache/dev/source-input" -P
                "${PROJECT_SOURCE_DIR}/tests/cmake/check_source_input_run.cmake")
        set_tests_properties(
            conformance.framework_sqlite_crud.source_input_run_once
            PROPERTIES LABELS "conformance;v8;source-input;framework;sqlite")
    endif()
endfunction()
