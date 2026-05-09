if(NOT DEFINED PROJECT_SOURCE_DIR)
    message(FATAL_ERROR "PROJECT_SOURCE_DIR is required")
endif()

function(require_file_substring path required message)
    file(READ "${path}" file_text)
    string(FIND "${file_text}" "${required}" found_index)
    if(found_index EQUAL -1)
        message(FATAL_ERROR "${message}: ${required}")
    endif()
endfunction()

set(matrix_doc "${PROJECT_SOURCE_DIR}/docs/contributor/testing.md")

if(NOT EXISTS "${matrix_doc}")
    message(FATAL_ERROR "Missing conformance evidence doc: ${matrix_doc}")
endif()

foreach(required IN ITEMS
        "Default non-V8"
        "V8-gated"
        "localhost transport"
        "SQLite/capability"
        "package outside-checkout"
        "live-provider optional"
        "stress/smoke"
        "benchmark harness"
        "Skipped optional gates are not pass claims"
        "skipped/not configured"
        "conformance.foundation.*"
        "conformance.v8.*"
        "conformance.http.*"
        "conformance.transport.*"
        "conformance.sqlite.*"
        "conformance.capability.*"
        "conformance.package.*"
        "smoke.*"
        "benchmark.*")
    require_file_substring(
        "${matrix_doc}" "${required}" "conformance matrix is missing required text")
endforeach()
