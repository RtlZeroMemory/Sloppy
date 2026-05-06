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

set(client_app "${PROJECT_SOURCE_DIR}/examples/net-tcp-client/app.js")
set(client_readme "${PROJECT_SOURCE_DIR}/examples/net-tcp-client/README.md")
set(server_app "${PROJECT_SOURCE_DIR}/examples/net-tcp-server/app.js")
set(server_readme "${PROJECT_SOURCE_DIR}/examples/net-tcp-server/README.md")
set(echo_app "${PROJECT_SOURCE_DIR}/examples/net-tcp-echo/app.js")
set(echo_readme "${PROJECT_SOURCE_DIR}/examples/net-tcp-echo/README.md")
set(policy_app "${PROJECT_SOURCE_DIR}/examples/net-policy-strict/app.js")
set(policy_readme "${PROJECT_SOURCE_DIR}/examples/net-policy-strict/README.md")
set(deadline_app "${PROJECT_SOURCE_DIR}/examples/net-deadline-cancel/app.js")
set(deadline_readme "${PROJECT_SOURCE_DIR}/examples/net-deadline-cancel/README.md")

foreach(required_file IN ITEMS
        "${client_app}" "${client_readme}"
        "${server_app}" "${server_readme}"
        "${echo_app}" "${echo_readme}"
        "${policy_app}" "${policy_readme}"
        "${deadline_app}" "${deadline_readme}")
    if(NOT EXISTS "${required_file}")
        message(FATAL_ERROR "Missing Network example file: ${required_file}")
    endif()
endforeach()

file(READ "${client_app}" client_source)
file(READ "${client_readme}" client_readme_source)
file(READ "${server_app}" server_source)
file(READ "${server_readme}" server_readme_source)
file(READ "${echo_app}" echo_source)
file(READ "${echo_readme}" echo_readme_source)
file(READ "${policy_app}" policy_source)
file(READ "${policy_readme}" policy_readme_source)
file(READ "${deadline_app}" deadline_source)
file(READ "${deadline_readme}" deadline_readme_source)

foreach(required_pattern IN ITEMS
        "import { TcpClient, NetworkAddress } from \"sloppy/net\";"
        "NetworkAddress.parse(\"127.0.0.1:6379\")"
        "await TcpClient.connect({"
        "host: target.host"
        "port: target.port"
        "noDelay: true"
        "keepAlive: { enabled: true, delayMs: 30000 }"
        "await conn.writeText(\"PING\\r\\n\");"
        "await conn.readLine({ deadline });"
        "await conn.close();")
    require_substring("${client_source}" "${required_pattern}"
                      "examples/net-tcp-client/app.js is missing expected TCP client API shape")
endforeach()

foreach(required_pattern IN ITEMS
        "import { TcpListener } from \"sloppy/net\";"
        "TcpListener.listen({"
        "host: \"127.0.0.1\""
        "port: 9000"
        "backlog: 128"
        "for await (const conn of listener.accept({ signal: appSignal }))"
        "await conn.readLine()"
        "await listener.close();")
    require_substring("${server_source}" "${required_pattern}"
                      "examples/net-tcp-server/app.js is missing expected listener API shape")
endforeach()

foreach(required_pattern IN ITEMS
        "import { TcpClient, TcpListener } from \"sloppy/net\";"
        "port: 0"
        "readChunks({ maxBytes: 65536 })"
        "await conn.write(chunk)"
        "listener.localAddress"
        "return await conn.read({ maxBytes: payload.length });")
    require_substring("${echo_source}" "${required_pattern}"
                      "examples/net-tcp-echo/app.js is missing expected echo API shape")
endforeach()

foreach(required_pattern IN ITEMS
        "strictNetworkPolicy"
        "mode: \"strict\""
        "access: \"connect\""
        "access: \"listen\""
        "TcpClient.connect({"
        "TcpListener.listen({")
    require_substring("${policy_source}" "${required_pattern}"
                      "examples/net-policy-strict/app.js is missing expected policy API shape")
endforeach()

foreach(required_pattern IN ITEMS
        "import { TcpClient } from \"sloppy/net\";"
        "import { CancellationController, Deadline } from \"sloppy/time\";"
        "Deadline.after(500)"
        "timeoutMs: 500"
        "signal: appSignal"
        "await conn.writeText(\"PING\\r\\n\", { deadline, signal: appSignal });"
        "controller.cancel(\"request cancelled\")")
    require_substring("${deadline_source}" "${required_pattern}"
                      "examples/net-deadline-cancel/app.js is missing expected deadline/cancel shape")
endforeach()

foreach(source IN ITEMS
        "${client_source}" "${server_source}" "${echo_source}" "${policy_source}"
        "${deadline_source}")
    foreach(forbidden_pattern IN ITEMS
            "node:net"
            "require(\"net\")"
            "from \"net\""
            "Bun."
            "Deno."
            "WebSocket"
            "dgram"
            "tls."
            "node:tls"
            "node:http"
            "node:https"
            "fetch("
            "console.log"
            "npm "
            "benchmark")
        reject_substring("${source}" "${forbidden_pattern}"
                         "Network examples must keep evidence boundaries clear")
    endforeach()
endforeach()

foreach(readme_source IN ITEMS
        "${client_readme_source}" "${server_readme_source}" "${echo_readme_source}"
        "${policy_readme_source}" "${deadline_readme_source}")
    foreach(required_pattern IN ITEMS
            "TLS"
            "HTTP"
            "UDP"
            "WebSocket"
            "Node/Bun/Deno"
            "package-manager"
            "public alpha"
            "benchmark")
        require_substring("${readme_source}" "${required_pattern}"
                          "Network example README is missing required boundary text")
    endforeach()
endforeach()
