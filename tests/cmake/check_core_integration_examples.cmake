if(NOT DEFINED PROJECT_SOURCE_DIR)
    message(FATAL_ERROR "PROJECT_SOURCE_DIR is required")
endif()

function(require_token haystack token description)
    string(FIND "${haystack}" "${token}" found_index)
    if(found_index EQUAL -1)
        message(FATAL_ERROR "${description}: ${token}")
    endif()
endfunction()

function(reject_regex haystack pattern description)
    string(REGEX MATCH "${pattern}" found_match "${haystack}")
    if(NOT "${found_match}" STREQUAL "")
        message(FATAL_ERROR "${description}: ${pattern}")
    endif()
endfunction()

set(example_names
    core-fs-time-codec
    core-network-time-codec
    core-process-time-codec
    core-worker-time
    core-policy-audit
    core-config-secrets)

foreach(example_name IN LISTS example_names)
    set(app_file "${PROJECT_SOURCE_DIR}/examples/${example_name}/app.js")
    set(readme_file "${PROJECT_SOURCE_DIR}/examples/${example_name}/README.md")
    if(NOT EXISTS "${app_file}")
        message(FATAL_ERROR "Missing Core integration example app: ${app_file}")
    endif()
    if(NOT EXISTS "${readme_file}")
        message(FATAL_ERROR "Missing Core integration example README: ${readme_file}")
    endif()

    file(READ "${app_file}" app_source)
    file(READ "${readme_file}" readme_source)
    foreach(forbidden_pattern IN ITEMS
            "(^|[^A-Za-z0-9_])Buffer\\."
            "(^|[^A-Za-z0-9_])node:"
            "(^|[^A-Za-z0-9_])require[ \t]*\\("
            "(^|[^A-Za-z0-9_])Bun\\."
            "(^|[^A-Za-z0-9_])Deno\\."
            "(^|[^A-Za-z0-9_])console\\.log[ \t]*\\(")
        reject_regex("${app_source}" "${forbidden_pattern}"
                     "Core integration examples must keep runtime boundaries clear")
    endforeach()
    foreach(required_pattern IN ITEMS
            "This example")
        require_token("${readme_source}" "${required_pattern}"
                      "Core integration README is missing required boundary text")
    endforeach()
    foreach(forbidden_pattern IN ITEMS
            "(^|[^A-Za-z0-9_-])production-ready([^A-Za-z0-9_-]|$)"
            "(^|[^A-Za-z0-9_-])performance improvement([^A-Za-z0-9_-]|$)"
            "(^|[^A-Za-z0-9_-])fastest([^A-Za-z0-9_-]|$)"
            "(^|[^A-Za-z0-9_-])compatible with Node([^A-Za-z0-9_-]|$)"
            "(^|[^A-Za-z0-9_-])compatible with Bun([^A-Za-z0-9_-]|$)"
            "(^|[^A-Za-z0-9_-])compatible with Deno([^A-Za-z0-9_-]|$)")
        reject_regex("${readme_source}" "${forbidden_pattern}"
                     "Core integration README overstates the current example scope")
    endforeach()
endforeach()

file(READ "${PROJECT_SOURCE_DIR}/examples/core-fs-time-codec/app.js" fs_source)
file(READ "${PROJECT_SOURCE_DIR}/examples/core-network-time-codec/app.js" net_source)
file(READ "${PROJECT_SOURCE_DIR}/examples/core-process-time-codec/app.js" process_source)
file(READ "${PROJECT_SOURCE_DIR}/examples/core-worker-time/app.js" worker_source)
file(READ "${PROJECT_SOURCE_DIR}/examples/core-policy-audit/app.js" policy_source)
file(READ "${PROJECT_SOURCE_DIR}/examples/core-config-secrets/app.js" config_source)

foreach(required_pattern IN ITEMS
        "sloppy/codec"
        "sloppy/fs"
        "sloppy/time"
        "Text.utf8.decode")
    require_token("${fs_source}" "${required_pattern}"
                  "core-fs-time-codec is missing expected API shape")
endforeach()

foreach(required_pattern IN ITEMS
        "sloppy/net"
        "Text.utf8.encode"
        "LocalEndpoint"
        "endpoint.read"
        "deadline"
        "signal")
    require_token("${net_source}" "${required_pattern}"
                  "core-network-time-codec is missing expected API shape")
endforeach()

foreach(required_pattern IN ITEMS
        "sloppy/os"
        "Process"
        "process.stdout.read"
        "Text.utf8.decode"
        "refreshStatusText")
    require_token("${process_source}" "${required_pattern}"
                  "core-process-time-codec is missing expected API shape")
endforeach()

foreach(required_pattern IN ITEMS
        "sloppy/workers"
        "Deadline.after"
        "WorkQueue"
        "backpressure")
    require_token("${worker_source}" "${required_pattern}"
                  "core-worker-time is missing expected API shape")
endforeach()

foreach(required_pattern IN ITEMS
        "Sloppy.module"
        "export const policyMetadata"
        "export default app"
        "capabilities.addDatabase"
        "mode: \"strict\""
        "Audit-only")
    require_token("${policy_source}" "${required_pattern}"
                  "core-policy-audit is missing expected API shape")
endforeach()

foreach(required_pattern IN ITEMS
        "config.getSecret(\"Provider:Token\")"
        "String(providerCredential)"
        "Results.json")
    require_token("${config_source}" "${required_pattern}"
                  "core-config-secrets is missing expected API shape")
endforeach()
