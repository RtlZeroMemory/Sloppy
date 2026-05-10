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

set(quickstart "${PROJECT_SOURCE_DIR}/docs/quickstart.md")
set(main_evidence "${PROJECT_SOURCE_DIR}/docs/contributor/testing.md")
set(quality_gates "${PROJECT_SOURCE_DIR}/docs/contributor/quality-gates.md")
set(cli_run "${PROJECT_SOURCE_DIR}/docs/cli/run.md")
set(cli_build "${PROJECT_SOURCE_DIR}/docs/cli/build.md")
set(compiler_doc "${PROJECT_SOURCE_DIR}/docs/internals/compiler.md")

foreach(path IN ITEMS "${quickstart}" "${main_evidence}" "${quality_gates}" "${cli_run}" "${cli_build}" "${compiler_doc}")
    if(NOT EXISTS "${path}")
        message(FATAL_ERROR "Missing MAIN contract doc: ${path}")
    endif()
endforeach()

foreach(required IN ITEMS
        "import { Sloppy, Results } from \"sloppy\";"
        "sloppy build"
        "sloppy run .sloppy --once GET /hello/Ada"
        "{\"hello\":\"Ada\"}"
        "app.plan.json")
    require_file_substring("${quickstart}" "${required}" "quickstart is missing MAIN contract text")
endforeach()

foreach(required IN ITEMS
        "Default (non-V8)"
        "V8-gated"
        "Package outside-checkout"
        "Live providers"
        "Benchmark")
    require_file_substring("${main_evidence}" "${required}" "MAIN evidence doc is missing text")
endforeach()

foreach(required IN ITEMS
        "Mandatory CI lanes"
        "Package outside-checkout"
        "`UNAVAILABLE`")
    require_file_substring("${quality_gates}" "${required}" "quality-gates doc is missing MAIN caveat")
endforeach()

foreach(required IN ITEMS
        "sloppy run"
        "--artifacts"
        "--once METHOD TARGET")
    require_file_substring("${cli_run}" "${required}" "cli/run doc is missing MAIN caveat")
endforeach()

foreach(required IN ITEMS
        "sloppy build"
        "app.plan.json"
        "deterministic")
    require_file_substring("${cli_build}" "${required}" "cli/build doc is missing MAIN caveat")
endforeach()

foreach(required IN ITEMS
        "deterministic Plan"
        "npm specifiers"
        "dynamic import()")
    require_file_substring("${compiler_doc}" "${required}" "compiler doc is missing MAIN caveat")
endforeach()
