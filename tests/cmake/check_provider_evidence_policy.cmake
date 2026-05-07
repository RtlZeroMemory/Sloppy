if(NOT DEFINED PROJECT_SOURCE_DIR)
    message(FATAL_ERROR "PROJECT_SOURCE_DIR is required")
endif()

set(required_files
    "docs/testing-strategy.md"
    "docs/quality-gates.md"
    "docs/data-providers.md"
    "docs/modules/data/README.md"
    "tests/conformance/providers/README.md"
    "tools/windows/test-live-providers.ps1"
    "tools/windows/test-live-postgres.ps1"
    "tools/windows/test-live-sqlserver.ps1"
    "tools/unix/test-live-providers.sh"
    "tools/unix/test-live-postgres.sh"
    "tools/unix/test-live-sqlserver.sh")

foreach(file IN LISTS required_files)
    if(NOT EXISTS "${PROJECT_SOURCE_DIR}/${file}")
        message(FATAL_ERROR "Provider evidence policy file is missing: ${file}")
    endif()
endforeach()

file(READ "${PROJECT_SOURCE_DIR}/docs/testing-strategy.md" testing_strategy)
file(READ "${PROJECT_SOURCE_DIR}/docs/quality-gates.md" quality_gates)
file(READ "${PROJECT_SOURCE_DIR}/docs/data-providers.md" data_providers)
file(READ "${PROJECT_SOURCE_DIR}/tests/conformance/providers/README.md" provider_conformance)
file(READ "${PROJECT_SOURCE_DIR}/.github/workflows/ci.yml" ci_workflow)

foreach(text_name testing_strategy quality_gates data_providers provider_conformance)
    set(text_value "${${text_name}}")
    foreach(required "skipped" "unavailable" "live-provider" "V8" "benchmark")
        string(FIND "${text_value}" "${required}" required_index)
        if(required_index EQUAL -1)
            message(FATAL_ERROR "${text_name} does not describe provider evidence term: ${required}")
        endif()
    endforeach()
endforeach()

foreach(required
        "live_provider"
        "live-postgres"
        "live-sqlserver"
        "live-providers"
        "SLOPPY_POSTGRES_TEST_URL"
        "SLOPPY_SQLSERVER_TEST_CONNECTION_STRING")
    string(FIND "${ci_workflow}" "${required}" required_index)
    if(required_index EQUAL -1)
        message(FATAL_ERROR "ci.yml does not expose provider live-lane trigger/reporting term: ${required}")
    endif()
endforeach()
