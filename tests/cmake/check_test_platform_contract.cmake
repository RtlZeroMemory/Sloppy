if(NOT DEFINED PROJECT_SOURCE_DIR)
    message(FATAL_ERROR "PROJECT_SOURCE_DIR is required")
endif()

set(required_pairs
    "AGENTS.md|Implementation Contract for Reviewers"
    "CONTRIBUTING.md|Evidence Reporting"
    "CONTRIBUTING.md|`PASS`"
    "CONTRIBUTING.md|`FAIL`"
    "CONTRIBUTING.md|`SKIPPED`"
    "CONTRIBUTING.md|`UNAVAILABLE`"
    ".github/PULL_REQUEST_TEMPLATE.md|Implementation Contract for Reviewers"
    ".github/PULL_REQUEST_TEMPLATE.md|Report skipped optional gates as skipped, unavailable, deferred, or not run."
    "docs/contributor/testing.md|Tests are how Sloppy verifies that a behavior change matches the docs"
    "docs/contributor/testing.md|Default (non-V8)"
    "docs/contributor/testing.md|Handler execution"
    "docs/contributor/testing.md|Fuzz / property"
    "docs/contributor/quality-gates.md|Package outside-checkout"
    "docs/contributor/quality-gates.md|`UNAVAILABLE`"
    "tests/golden/README.md|assert stable semantic fields"
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
