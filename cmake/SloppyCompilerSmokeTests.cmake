# sloppyc smoke tests registered from CMake. Included by cmake/SloppyTests.cmake.

    if(CARGO_EXECUTABLE)
        add_test(
            NAME sloppyc.cli.version
            COMMAND "${CARGO_EXECUTABLE}" run --manifest-path
                    "${PROJECT_SOURCE_DIR}/compiler/Cargo.toml" -- --version
            WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}")
        set_tests_properties(sloppyc.cli.version PROPERTIES PASS_REGULAR_EXPRESSION "sloppyc")
        add_test(
            NAME sloppyc.supported_app_pipeline
            COMMAND "${CARGO_EXECUTABLE}" test --manifest-path
                    "${PROJECT_SOURCE_DIR}/compiler/Cargo.toml"
            WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}")
    endif()
