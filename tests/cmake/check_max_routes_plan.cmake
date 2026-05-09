if(NOT DEFINED PROJECT_SOURCE_DIR)
    message(FATAL_ERROR "PROJECT_SOURCE_DIR is required")
endif()
if(NOT DEFINED SLOPPY_TEST_BINARY_DIR)
    message(FATAL_ERROR "SLOPPY_TEST_BINARY_DIR is required")
endif()
if(NOT DEFINED SLOPPY_CLI)
    message(FATAL_ERROR "SLOPPY_CLI is required")
endif()
if(NOT DEFINED SLOPPY_ROUTE_COUNT)
    set(SLOPPY_ROUTE_COUNT 1024)
endif()

math(EXPR max_route_index "${SLOPPY_ROUTE_COUNT} - 1")
set(artifact_dir "${SLOPPY_TEST_BINARY_DIR}/max-routes-plan-${SLOPPY_ROUTE_COUNT}")
set(plan_path "${artifact_dir}/app.plan.json")
set(app_js_path "${artifact_dir}/app.js")
set(source_map_path "${artifact_dir}/app.js.map")

file(REMOVE_RECURSE "${artifact_dir}")
file(MAKE_DIRECTORY "${artifact_dir}")

file(WRITE "${app_js_path}" [=[
const __sloppyRuntime = globalThis.__sloppy_runtime;
if (__sloppyRuntime === undefined) {
  throw new Error("Sloppy bootstrap runtime was not loaded");
}
const { Results } = __sloppyRuntime;

globalThis.__sloppy_handler_1 = () => Results.text("max-routes-ok");
globalThis.__sloppy_register_handler(1, globalThis.__sloppy_handler_1);
]=])
file(WRITE "${source_map_path}" "{\"version\":3,\"file\":\"app.js\",\"sources\":[],\"sourcesContent\":[],\"names\":[],\"mappings\":\"\"}\n")
file(SHA256 "${app_js_path}" app_js_hash)
file(SHA256 "${source_map_path}" source_map_hash)

set(plan_text "{\n")
string(APPEND plan_text "  \"bundle\": {\n")
string(APPEND plan_text "    \"hash\": \"sha256:${app_js_hash}\",\n")
string(APPEND plan_text "    \"id\": \"max-routes-app-js\",\n")
string(APPEND plan_text "    \"path\": \"app.js\"\n")
string(APPEND plan_text "  },\n")
string(APPEND plan_text "  \"compilerVersion\": \"max-routes-test-fixture\",\n")
string(APPEND plan_text "  \"handlers\": [\n")
string(APPEND plan_text "    { \"displayName\": \"GET max-routes\", \"exportName\": \"__sloppy_handler_1\", \"id\": 1 }\n")
string(APPEND plan_text "  ],\n")
string(APPEND plan_text "  \"routes\": [\n")
foreach(route_index RANGE 0 ${max_route_index})
    if(route_index GREATER 0)
        string(APPEND plan_text ",\n")
    endif()
    string(APPEND plan_text
           "    { \"handlerId\": 1, \"method\": \"GET\", \"name\": null, \"pattern\": \"/r${route_index}\" }")
endforeach()
string(APPEND plan_text "\n  ],\n")
string(APPEND plan_text "  \"runtimeMinimumVersion\": \"0.1.0\",\n")
string(APPEND plan_text "  \"schemaVersion\": 1,\n")
string(APPEND plan_text "  \"sourceMap\": {\n")
string(APPEND plan_text "    \"hash\": \"sha256:${source_map_hash}\",\n")
string(APPEND plan_text "    \"id\": \"max-routes-app-js-map\",\n")
string(APPEND plan_text "    \"path\": \"app.js.map\"\n")
string(APPEND plan_text "  },\n")
string(APPEND plan_text "  \"stdlibVersion\": \"0.1.0\",\n")
string(APPEND plan_text "  \"target\": { \"engine\": \"v8\", \"platform\": \"windows-x64\" }\n")
string(APPEND plan_text "}\n")
file(WRITE "${plan_path}" "${plan_text}")

function(run_cli_tool description)
    execute_process(
        COMMAND "${SLOPPY_CLI}" ${ARGN}
        WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
        RESULT_VARIABLE result
        OUTPUT_VARIABLE stdout
        ERROR_VARIABLE stderr)
    if(NOT result EQUAL 0)
        message(
            FATAL_ERROR
                "${description} failed for ${SLOPPY_ROUTE_COUNT} routes\nstdout:\n${stdout}\nstderr:\n${stderr}")
    endif()
    set(run_cli_tool_stdout "${stdout}" PARENT_SCOPE)
endfunction()

function(require_match text pattern description)
    if(NOT "${text}" MATCHES "${pattern}")
        message(FATAL_ERROR "${description} did not match ${pattern}\n${text}")
    endif()
endfunction()

run_cli_tool("sloppy routes" routes --plan "${plan_path}" --format json)
require_match("${run_cli_tool_stdout}" "\"/r0\"" "routes output")
require_match("${run_cli_tool_stdout}" "\"/r${max_route_index}\"" "routes output")

run_cli_tool("sloppy doctor" doctor --plan "${plan_path}" --format text)
require_match("${run_cli_tool_stdout}" "route metadata present" "doctor output")

run_cli_tool("sloppy openapi" openapi --plan "${plan_path}")
require_match("${run_cli_tool_stdout}" "\"/r0\"" "OpenAPI output")
require_match("${run_cli_tool_stdout}" "\"/r${max_route_index}\"" "OpenAPI output")

if(SLOPPY_CHECK_RUN)
    run_cli_tool("sloppy run --once" run --artifacts "${artifact_dir}" --once GET
                 "/r${max_route_index}")
    require_match("${run_cli_tool_stdout}" "max-routes-ok" "run output")
endif()
