cmake_minimum_required(VERSION 3.20)

if(NOT DEFINED SLOPPYC_PATH AND DEFINED SLOPPYC_EXECUTABLE)
  set(SLOPPYC_PATH "${SLOPPYC_EXECUTABLE}")
endif()

if(NOT DEFINED SLOPPY_PATH AND DEFINED SLOPPY_CLI)
  set(SLOPPY_PATH "${SLOPPY_CLI}")
endif()

if(NOT DEFINED TEST_WORK_DIR AND DEFINED CMAKE_BINARY_DIR)
  set(TEST_WORK_DIR "${CMAKE_BINARY_DIR}/static-assets-work")
endif()

if(NOT DEFINED SLOPPYC_PATH)
  message(FATAL_ERROR "SLOPPYC_PATH or SLOPPYC_EXECUTABLE is required")
endif()

if(NOT DEFINED SLOPPY_PATH)
  message(FATAL_ERROR "SLOPPY_PATH or SLOPPY_CLI is required")
endif()

if(NOT DEFINED TEST_WORK_DIR)
  message(FATAL_ERROR "TEST_WORK_DIR or CMAKE_BINARY_DIR is required")
endif()

file(REMOVE_RECURSE "${TEST_WORK_DIR}")
file(MAKE_DIRECTORY "${TEST_WORK_DIR}")

set(project_dir "${TEST_WORK_DIR}/static-app")
file(MAKE_DIRECTORY
  "${project_dir}/public/css"
  "${project_dir}/public/img"
  "${project_dir}/public/wasm")

file(WRITE "${project_dir}/sloppy.json" [=[
{
  "routes": [
    { "method": "GET", "path": "/health", "handler": "health" }
  ],
  "staticFiles": [
    {
      "requestPath": "/public",
      "root": "public",
      "cache": { "maxAgeSeconds": 3600 }
    }
  ]
}
]=])

file(WRITE "${project_dir}/app.ts" [=[
import { Results, Sloppy } from "sloppy";

const app = Sloppy.create();
app.get("/health", () => Results.text("ok"));
app.useStaticFiles({
  requestPath: "/public",
  root: "public",
  cache: { maxAgeSeconds: 3600 }
});
export default app;
]=])

file(WRITE "${project_dir}/public/index.html" "<!doctype html><title>static</title>")
file(WRITE "${project_dir}/public/css/site.css" "body { color: #123456; }\n")
file(WRITE "${project_dir}/public/data.json" "{\"ok\":true}\n")
file(WRITE "${project_dir}/public/img/pixel.png" "png-bytes")
file(WRITE "${project_dir}/public/wasm/module.wasm" "wasm-bytes")
set(outside_secret_marker "__outside-static-root-secret__")
file(WRITE "${project_dir}/secret.txt" "${outside_secret_marker}\n")

function(run_static_command description working_dir expected_output)
  execute_process(
    COMMAND ${ARGN}
    WORKING_DIRECTORY "${working_dir}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE error)

  if(NOT result EQUAL 0)
    message(FATAL_ERROR "${description} failed with ${result}\nSTDOUT:\n${output}\nSTDERR:\n${error}")
  endif()

  if(NOT "${expected_output}" STREQUAL "" AND NOT output MATCHES "${expected_output}")
    message(FATAL_ERROR "${description} output did not match ${expected_output}\nSTDOUT:\n${output}\nSTDERR:\n${error}")
  endif()

  set(last_static_stdout "${output}" PARENT_SCOPE)
  set(last_static_stderr "${error}" PARENT_SCOPE)
endfunction()

run_static_command(
  "compile"
  "${project_dir}"
  ""
  "${SLOPPYC_PATH}" build app.ts --out build)

set(plan_path "${project_dir}/build/app.plan.json")
if(NOT EXISTS "${plan_path}")
  message(FATAL_ERROR "compile did not create ${plan_path}")
endif()

file(READ "${plan_path}" plan_json)
foreach(expected
    "/public/index.html"
    "/public/css/site.css"
    "/public/data.json"
    "/public/img/pixel.png"
    "/public/wasm/module.wasm"
    "public/index.html"
    "public/css/site.css"
    "public/data.json"
    "public/img/pixel.png"
    "public/wasm/module.wasm"
    "\"assets\""
    "\"dependencyGraph\"")
  if(NOT plan_json MATCHES "${expected}")
    message(FATAL_ERROR "compiled plan missing ${expected}\n${plan_json}")
  endif()
endforeach()

file(READ "${project_dir}/build/app.js" app_js)
foreach(expected
    "contentType: \"text/html"
    "contentType: \"text/css"
    "contentType: \"application/json"
    "contentType: \"image/png"
    "contentType: \"application/wasm"
    "Cache-Control\":\"public, max-age=3600")
  if(NOT app_js MATCHES "${expected}")
    message(FATAL_ERROR "compiled app.js missing ${expected}\n${app_js}")
  endif()
endforeach()

run_static_command(
  "package"
  "${project_dir}"
  ""
  "${SLOPPY_PATH}" package app.ts --out dist/static-app)

foreach(expected_asset
    "${project_dir}/dist/static-app/package/artifacts/assets/public/index.html"
    "${project_dir}/dist/static-app/package/artifacts/assets/public/css/site.css"
    "${project_dir}/dist/static-app/package/artifacts/assets/public/data.json"
    "${project_dir}/dist/static-app/package/artifacts/assets/public/img/pixel.png"
    "${project_dir}/dist/static-app/package/artifacts/assets/public/wasm/module.wasm")
  if(NOT EXISTS "${expected_asset}")
    message(FATAL_ERROR "package did not copy ${expected_asset}")
  endif()
endforeach()

if(NOT SLOPPY_ENABLE_V8)
  return()
endif()

set(outside_dir "${TEST_WORK_DIR}/outside")
file(REMOVE_RECURSE "${outside_dir}")
file(MAKE_DIRECTORY "${outside_dir}")
file(COPY "${project_dir}/dist/static-app/package" DESTINATION "${outside_dir}")

function(run_packaged_request description path)
  run_static_command(
    "${description}"
    "${outside_dir}"
    "HTTP/1.1 200 OK"
    "${SLOPPY_PATH}" run package --once GET "${path}")

  set(response "${last_static_stdout}")
  foreach(expected ${ARGN})
    if(NOT response MATCHES "${expected}")
      message(FATAL_ERROR "${description} response missing ${expected}\n${response}")
    endif()
  endforeach()
endfunction()

run_packaged_request(
  "serve html asset outside checkout"
  "/public/index.html"
  "Content-Type: text/html"
  "Cache-Control: public, max-age=3600"
  "ETag: \\\""
  "<!doctype html><title>static</title>")

run_packaged_request(
  "serve css asset outside checkout"
  "/public/css/site.css"
  "Content-Type: text/css"
  "body \\{ color: #123456; \\}")

run_packaged_request(
  "serve wasm asset outside checkout"
  "/public/wasm/module.wasm"
  "Content-Type: application/wasm"
  "wasm-bytes")

execute_process(
  COMMAND "${SLOPPY_PATH}" run package --once GET "/public/../secret.txt"
  WORKING_DIRECTORY "${outside_dir}"
  RESULT_VARIABLE traversal_result
  OUTPUT_VARIABLE traversal_output
  ERROR_VARIABLE traversal_error)

if(NOT traversal_result EQUAL 0)
  message(FATAL_ERROR "traversal request command failed with ${traversal_result}\nSTDOUT:\n${traversal_output}\nSTDERR:\n${traversal_error}")
endif()

if(traversal_output MATCHES "${outside_secret_marker}")
  message(FATAL_ERROR "traversal response leaked asset contents\n${traversal_output}")
endif()

if(NOT traversal_output MATCHES "HTTP/1.1 404 Not Found")
  message(FATAL_ERROR "traversal response should be 404\n${traversal_output}")
endif()
