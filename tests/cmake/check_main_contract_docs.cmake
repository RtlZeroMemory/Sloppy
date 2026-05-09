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

set(first_api "${PROJECT_SOURCE_DIR}/docs/tutorials/first-api.md")
set(main_evidence "${PROJECT_SOURCE_DIR}/docs/contributor/testing.md")
set(public_cli "${PROJECT_SOURCE_DIR}/docs/reference/cli.md")
set(compiler_doc "${PROJECT_SOURCE_DIR}/docs/internals/compiler.md")

foreach(path IN ITEMS "${first_api}" "${main_evidence}" "${public_cli}" "${compiler_doc}")
    if(NOT EXISTS "${path}")
        message(FATAL_ERROR "Missing MAIN contract doc: ${path}")
    endif()
endforeach()

foreach(required IN ITEMS
        "import { Sloppy, Results } from \"sloppy\";"
        "sloppy build"
        "sloppy run --artifacts .sloppy --once GET /hello/Ada"
        "{\"hello\":\"Ada\"}"
        "The runtime executes the emitted artifacts")
    require_file_substring("${first_api}" "${required}" "first API tutorial is missing MAIN contract text")
endforeach()

foreach(required IN ITEMS
        "Default non-V8"
        "V8-gated"
        "package outside-checkout"
        "live-network/live-provider"
        "benchmark"
        "Report skipped or unavailable lanes honestly as not run")
    require_file_substring("${main_evidence}" "${required}" "MAIN evidence doc is missing text")
endforeach()

foreach(required IN ITEMS
        "`sloppy run <source.js|source.mjs|source.ts>` invokes `sloppyc build`"
        "package manager"
        "npm-style dependency resolution"
        "Source input is limited")
    require_file_substring("${public_cli}" "${required}" "public CLI doc is missing MAIN caveat")
endforeach()

foreach(required IN ITEMS
        "deterministic artifacts"
        "Node package resolution"
        "dynamic route strings")
    require_file_substring("${compiler_doc}" "${required}" "compiler doc is missing MAIN caveat")
endforeach()
