if(NOT DEFINED PROJECT_SOURCE_DIR)
    message(FATAL_ERROR "PROJECT_SOURCE_DIR is required")
endif()

set(required_pairs
    "AGENTS.md|Future PR test evidence"
    "CONTRIBUTING.md|Evidence Lane Report"
    ".github/PULL_REQUEST_TEMPLATE.md|Implementation Contract for Reviewers"
    ".github/PULL_REQUEST_TEMPLATE.md|Skipped optional gates are not pass claims."
    "docs/testing-strategy.md|TEST-PLATFORM-01"
    "docs/testing-strategy.md|default non-V8"
    "docs/testing-strategy.md|V8-gated"
    "docs/testing-strategy.md|fuzz/property"
    "docs/quality-gates.md|package outside-checkout"
    "docs/quality-gates.md|PASS, FAIL, SKIPPED, UNAVAILABLE"
    "tests/golden/README.md|semantic contract"
    "tests/fuzz/README.md|libFuzzer"
    "tests/fuzz/README.md|seed replay"
    "tests/conformance/cross-api/README.md|#652"
    "tests/conformance/v8/bridge-test-template.md|no raw native handle"
)

foreach(pair IN LISTS required_pairs)
    string(REPLACE "|" ";" parts "${pair}")
    list(GET parts 0 relative_path)
    list(GET parts 1 needle)
    set(path "${PROJECT_SOURCE_DIR}/${relative_path}")
    if(NOT EXISTS "${path}")
        message(FATAL_ERROR "required TEST-PLATFORM-01 file is missing: ${relative_path}")
    endif()

    file(READ "${path}" text)
    string(FIND "${text}" "${needle}" found)
    if(found LESS 0)
        message(FATAL_ERROR "${relative_path} is missing required TEST-PLATFORM-01 text: ${needle}")
    endif()
endforeach()
