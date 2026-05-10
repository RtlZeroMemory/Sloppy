# Alpha golden and app-flow proof tests. Included by cmake/SloppyTests.cmake.

if(CARGO_EXECUTABLE AND SLOPPY_BUILD_COMPILER AND SLOPPY_ENABLE_V8)
    function(sloppy_add_alpha_proof_test test_name area)
        set(alpha_proof_args
            "--root" "${PROJECT_SOURCE_DIR}"
            "--sloppy" "$<TARGET_FILE:sloppy>"
            "--sloppyc" "${SLOPPYC_BUILT_EXECUTABLE}"
            "--area" "${area}"
            "--work-root" "${CMAKE_BINARY_DIR}/alpha-proof/${test_name}"
            "--require-v8")
        list(APPEND alpha_proof_args ${ARGN})

        add_test(
            NAME ${test_name}
            COMMAND "$<TARGET_FILE:sloppy>" "run" "${PROJECT_SOURCE_DIR}/tools/golden/alpha-proof.ts"
                    "--" ${alpha_proof_args})
        set_tests_properties(${test_name} PROPERTIES WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
                                                     LABELS "golden;alpha-proof;${area}")
    endfunction()

    foreach(section IN ITEMS help web program)
        sloppy_add_alpha_proof_test(alpha.golden.cli.${section} cli "--cli-section" "${section}")
    endforeach()
    foreach(case_name IN ITEMS
                         hello-mapget
                         grouped-route
                         http-methods
                         framework-metadata
                         full-framework-app-graph
                         realistic-users-api
                         provider-capability
                         partial-body-without-schema
                         function-module
                         source-map)
        sloppy_add_alpha_proof_test(alpha.golden.compiler.${case_name} compiler
                                    "--compiler-case" "${case_name}")
    endforeach()
    foreach(template IN ITEMS minimal-api full-api dogfood program)
        sloppy_add_alpha_proof_test(alpha.golden.templates.${template} templates "--template"
                                    "${template}")
        set_tests_properties(alpha.golden.templates.${template} PROPERTIES LABELS
                                                                           "golden;alpha-proof;templates;package")
    endforeach()
    sloppy_add_alpha_proof_test(alpha.golden.diagnostics diagnostics)
    set_tests_properties(alpha.golden.diagnostics PROPERTIES LABELS
                                                             "golden;alpha-proof;diagnostic")
    foreach(flow IN ITEMS minimal-api full-api dogfood program direct-program direct-web)
        sloppy_add_alpha_proof_test(alpha_flow.core.${flow} alpha-flows "--flow" "${flow}")
        set_tests_properties(alpha_flow.core.${flow} PROPERTIES LABELS
                                                                "golden;alpha-proof;alpha-flows;integration;package;program")
    endforeach()
    sloppy_add_alpha_proof_test(alpha.examples.classification examples "--example"
                                "classification")
    set_tests_properties(alpha.examples.classification PROPERTIES LABELS
                                                                  "golden;alpha-proof;examples;package")
    foreach(example IN ITEMS
                         compiler-hello
                         hello-minimal
                         configured-api
                         modules-api
                         validation-errors
                         request-context
                         users-api-sqlite
                         prealpha-control-plane
                         framework-hello
                         framework-validation-errors
                         framework-explicit-binding
                         framework-di-services
                         framework-sqlite-crud
                         program-hello
                         program-fs-process)
        sloppy_add_alpha_proof_test(alpha.examples.${example} examples "--example" "${example}")
        set_tests_properties(alpha.examples.${example} PROPERTIES LABELS
                                                                  "golden;alpha-proof;examples;package")
    endforeach()
endif()
