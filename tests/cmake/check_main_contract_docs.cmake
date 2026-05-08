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

set(roadmap_main "${PROJECT_SOURCE_DIR}/docs/project/roadmap-main.md")
set(main_evidence "${PROJECT_SOURCE_DIR}/docs/project/main-evidence.md")
set(public_cli "${PROJECT_SOURCE_DIR}/docs/public/cli.md")
set(compiler_doc "${PROJECT_SOURCE_DIR}/docs/compiler.md")

foreach(path IN ITEMS "${roadmap_main}" "${main_evidence}" "${public_cli}" "${compiler_doc}")
    if(NOT EXISTS "${path}")
        message(FATAL_ERROR "Missing MAIN contract doc: ${path}")
    endif()
endforeach()

foreach(required IN ITEMS
        "import { Sloppy, Results } from \"sloppy\";"
        "sloppyc build examples/compiler-hello/app.js --out .sloppy-main-smoke"
        "sloppy run --artifacts .sloppy-main-smoke --once GET /"
        "Hello from Sloppy"
        "Source-input `sloppy run <source.js>`"
        "Node/npm/package-manager behavior is not part of MAIN")
    require_file_substring("${roadmap_main}" "${required}" "roadmap MAIN contract is missing text")
endforeach()

foreach(required IN ITEMS
        "Default non-V8"
        "default non-V8 results"
        "V8-Gated Evidence"
        "These commands do not prove"
        "Package Evidence"
        "Live Provider Evidence"
        "Benchmark Evidence"
        "public alpha readiness")
    require_file_substring("${main_evidence}" "${required}" "MAIN evidence doc is missing text")
endforeach()

foreach(required IN ITEMS
        "`sloppy run <source.js|source.mjs|source.ts>` invokes `sloppyc build`"
        "no package manager"
        "no npm resolution"
        "no Node compatibility")
    require_file_substring("${public_cli}" "${required}" "public CLI doc is missing MAIN caveat")
endforeach()

foreach(required IN ITEMS
        "deterministic artifacts"
        "does not implement Node package"
        "dynamic route strings")
    require_file_substring("${compiler_doc}" "${required}" "compiler doc is missing MAIN caveat")
endforeach()
