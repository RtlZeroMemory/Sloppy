if(NOT DEFINED PROJECT_SOURCE_DIR)
    message(FATAL_ERROR "PROJECT_SOURCE_DIR is required")
endif()

set(required_pairs
    "AGENTS.md|Implementation Contract for Reviewers"
    "CONTRIBUTING.md|Evidence Reporting"
    ".github/PULL_REQUEST_TEMPLATE.md|Implementation Contract for Reviewers"
    ".github/PULL_REQUEST_TEMPLATE.md|Skipped optional gates are not pass claims."
    "docs/testing-strategy.md|Tests are executable specifications"
    "docs/testing-strategy.md|default non-V8"
    "docs/testing-strategy.md|V8-gated"
    "docs/testing-strategy.md|fuzz/property"
    "docs/quality-gates.md|package outside-checkout"
    "docs/quality-gates.md|`PASS`, `FAIL`, `SKIPPED`, `UNAVAILABLE`"
    "tests/golden/README.md|semantic contract"
    "tests/fuzz/README.md|libFuzzer"
    "tests/fuzz/README.md|seed replay"
    "tests/conformance/cross-api/README.md|cross-API conformance scenarios"
    "tests/conformance/v8/bridge-test-template.md|no raw native handle"
)

foreach(pair IN LISTS required_pairs)
    string(REPLACE "|" ";" parts "${pair}")
    list(GET parts 0 relative_path)
    list(GET parts 1 needle)
    set(path "${PROJECT_SOURCE_DIR}/${relative_path}")
    if(NOT EXISTS "${path}")
        message(FATAL_ERROR "required governance file is missing: ${relative_path}")
    endif()

    file(READ "${path}" text)
    string(FIND "${text}" "${needle}" found)
    if(found LESS 0)
        message(FATAL_ERROR "${relative_path} is missing required governance text: ${needle}")
    endif()
endforeach()
