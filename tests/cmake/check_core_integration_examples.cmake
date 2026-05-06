if(NOT DEFINED PROJECT_SOURCE_DIR)
    message(FATAL_ERROR "PROJECT_SOURCE_DIR is required")
endif()

function(require_substring haystack needle description)
    string(FIND "${haystack}" "${needle}" found_index)
    if(found_index EQUAL -1)
        message(FATAL_ERROR "${description}: ${needle}")
    endif()
endfunction()

function(reject_substring haystack needle description)
    string(FIND "${haystack}" "${needle}" found_index)
    if(NOT found_index EQUAL -1)
        message(FATAL_ERROR "${description}: ${needle}")
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
            "Buffer."
            "node:"
            "require("
            "Bun."
            "Deno."
            "console.log")
        reject_substring("${app_source}" "${forbidden_pattern}"
                         "Core integration examples must keep runtime boundaries clear")
    endforeach()
    foreach(required_pattern IN ITEMS
            "Status: CORE-INTEGRATION-01"
            "no public alpha claim"
            "no benchmark")
        require_substring("${readme_source}" "${required_pattern}"
                          "Core integration README is missing required boundary text")
    endforeach()
    foreach(forbidden_pattern IN ITEMS
            "production-ready"
            "performance improvement"
            "fastest"
            "compatible with Node"
            "compatible with Bun"
            "compatible with Deno")
        reject_substring("${readme_source}" "${forbidden_pattern}"
                         "Core integration README must not overclaim")
    endforeach()
endforeach()

file(READ "${PROJECT_SOURCE_DIR}/examples/core-fs-time-codec/app.js" fs_source)
file(READ "${PROJECT_SOURCE_DIR}/examples/core-network-time-codec/app.js" net_source)
file(READ "${PROJECT_SOURCE_DIR}/examples/core-process-time-codec/app.js" process_source)
file(READ "${PROJECT_SOURCE_DIR}/examples/core-worker-time/app.js" worker_source)
file(READ "${PROJECT_SOURCE_DIR}/examples/core-policy-audit/app.js" policy_source)
file(READ "${PROJECT_SOURCE_DIR}/examples/core-config-secrets/app.js" config_source)

foreach(required_pattern IN ITEMS
        "import { Text } from \"sloppy/codec\";"
        "import { File } from \"sloppy/fs\";"
        "import { Deadline } from \"sloppy/time\";"
        "Text.utf8.decode(bytes, { fatal: true })")
    require_substring("${fs_source}" "${required_pattern}"
                      "core-fs-time-codec is missing expected API shape")
endforeach()

foreach(required_pattern IN ITEMS
        "import { LocalEndpoint } from \"sloppy/net\";"
        "Text.utf8.encode(\"status"
        "endpoint.read({ maxBytes: 4096, deadline, signal })")
    require_substring("${net_source}" "${required_pattern}"
                      "core-network-time-codec is missing expected API shape")
endforeach()

foreach(required_pattern IN ITEMS
        "import { Process } from \"sloppy/os\";"
        "process.stdout.read(4096)"
        "Text.utf8.decode(output, { fatal: true })")
    require_substring("${process_source}" "${required_pattern}"
                      "core-process-time-codec is missing expected API shape")
endforeach()

foreach(required_pattern IN ITEMS
        "import { WorkQueue } from \"sloppy/workers\";"
        "Deadline.after(500)"
        "overflow: \"backpressure\"")
    require_substring("${worker_source}" "${required_pattern}"
                      "core-worker-time is missing expected API shape")
endforeach()

foreach(required_pattern IN ITEMS
        "Sloppy.module(\"core-policy\")"
        "capabilities.addDatabase(\"data.main\""
        "mode: \"strict\"")
    require_substring("${policy_source}" "${required_pattern}"
                      "core-policy-audit is missing expected API shape")
endforeach()

foreach(required_pattern IN ITEMS
        "config.getSecret(\"Provider:Token\")"
        "String(providerCredential)"
        "Results.json")
    require_substring("${config_source}" "${required_pattern}"
                      "core-config-secrets is missing expected API shape")
endforeach()
