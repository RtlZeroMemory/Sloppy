if(NOT DEFINED PROJECT_SOURCE_DIR)
    message(FATAL_ERROR "PROJECT_SOURCE_DIR is required")
endif()
if(NOT DEFINED CMAKE_BINARY_DIR)
    message(FATAL_ERROR "CMAKE_BINARY_DIR is required")
endif()
if(NOT DEFINED SLOPPY_CLI)
    message(FATAL_ERROR "SLOPPY_CLI is required")
endif()
if(NOT DEFINED SLOPPYC_EXECUTABLE)
    message(FATAL_ERROR "SLOPPYC_EXECUTABLE is required")
endif()
if(NOT DEFINED SLOPPY_EXPECT_RUN_SUCCESS)
    set(SLOPPY_EXPECT_RUN_SUCCESS OFF)
endif()

set(work_dir "${CMAKE_BINARY_DIR}/program-artifact-shorthand")
set(simple_dir "${work_dir}/simple")
set(capability_dir "${work_dir}/capability")
set(bytes_dir "${work_dir}/bytes")
file(REMOVE_RECURSE "${work_dir}")
file(MAKE_DIRECTORY "${simple_dir}/src")
file(MAKE_DIRECTORY "${capability_dir}/src")
file(MAKE_DIRECTORY "${bytes_dir}/src")

file(WRITE "${simple_dir}/src/message.ts" "export const message: string = \"hello from program shorthand\";\n")
file(WRITE "${simple_dir}/src/main.ts" [=[
import { message } from "./message";

type ProgramResult = string;

export async function main(): Promise<ProgramResult> {
  return message;
}
]=])

file(WRITE "${capability_dir}/src/main.ts" [=[
import {
  File
} from "sloppy/fs";

export function main() {
  return typeof File;
}
]=])

file(WRITE "${bytes_dir}/src/main.ts" [=[
export function main() {
  return {
    __sloppyResult: true,
    kind: "bytes",
    status: 200,
    contentType: "application/octet-stream",
    body: new Uint8Array([65, 66])
  };
}
]=])

function(run_cli_expect_success description working_dir expected_pattern)
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E env "SLOPPY_SLOPPYC=${SLOPPYC_EXECUTABLE}" "${SLOPPY_CLI}"
                ${ARGN}
        WORKING_DIRECTORY "${working_dir}"
        TIMEOUT 180
        RESULT_VARIABLE result
        OUTPUT_VARIABLE stdout
        ERROR_VARIABLE stderr)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "${description} failed\nstdout:\n${stdout}\nstderr:\n${stderr}")
    endif()
    if(expected_pattern)
        string(REGEX MATCH "${expected_pattern}" matched "${stdout}")
        if(NOT matched)
            message(FATAL_ERROR "${description} output did not match '${expected_pattern}'\nstdout:\n${stdout}\nstderr:\n${stderr}")
        endif()
    endif()
    set(RUN_CLI_STDOUT "${stdout}" PARENT_SCOPE)
    set(RUN_CLI_STDERR "${stderr}" PARENT_SCOPE)
endfunction()

function(run_cli_expect_failure description working_dir expected_pattern)
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E env "SLOPPY_SLOPPYC=${SLOPPYC_EXECUTABLE}" "${SLOPPY_CLI}"
                ${ARGN}
        WORKING_DIRECTORY "${working_dir}"
        TIMEOUT 180
        RESULT_VARIABLE result
        OUTPUT_VARIABLE stdout
        ERROR_VARIABLE stderr)
    if(result EQUAL 0)
        message(FATAL_ERROR "${description} unexpectedly succeeded\nstdout:\n${stdout}\nstderr:\n${stderr}")
    endif()
    if(NOT stderr MATCHES "${expected_pattern}")
        message(FATAL_ERROR "${description} error did not match '${expected_pattern}'\nstdout:\n${stdout}\nstderr:\n${stderr}")
    endif()
endfunction()

run_cli_expect_success("sloppy build src/main.ts" "${simple_dir}" "" build src/main.ts)
foreach(artifact IN ITEMS app.plan.json app.js app.js.map)
    if(NOT EXISTS "${simple_dir}/.sloppy/${artifact}")
        message(FATAL_ERROR "sloppy build src/main.ts did not emit .sloppy/${artifact}")
    endif()
endforeach()
file(READ "${simple_dir}/.sloppy/app.plan.json" simple_plan)
if(NOT simple_plan MATCHES "\"kind\": \"program\"")
    message(FATAL_ERROR "zero-config program source did not emit Program Plan\n${simple_plan}")
endif()
if(NOT simple_plan MATCHES "\"routes\"[ \t\r\n]*:[ \t\r\n]*\\[[ \t\r\n]*\\]")
    message(FATAL_ERROR "Program Plan did not emit empty routes\n${simple_plan}")
endif()

run_cli_expect_success("sloppy package src/main.ts" "${simple_dir}" "Created Sloppy app package" package src/main.ts)
foreach(packaged IN ITEMS manifest.json artifacts/app.plan.json artifacts/app.js artifacts/app.js.map)
    if(NOT EXISTS "${simple_dir}/.sloppy/package/${packaged}")
        message(FATAL_ERROR "sloppy package src/main.ts did not emit .sloppy/package/${packaged}")
    endif()
endforeach()
file(READ "${simple_dir}/.sloppy/package/artifacts/app.plan.json" package_plan)
if(NOT package_plan MATCHES "\"kind\": \"program\"")
    message(FATAL_ERROR "sloppy package src/main.ts did not package Program Plan\n${package_plan}")
endif()

if(SLOPPY_EXPECT_RUN_SUCCESS)
    run_cli_expect_success("sloppy run src/main.ts" "${simple_dir}" "hello from program shorthand" run src/main.ts)
    run_cli_expect_success("sloppy run .sloppy" "${simple_dir}" "hello from program shorthand" run .sloppy)
    run_cli_expect_success("sloppy run fallback .sloppy" "${simple_dir}" "hello from program shorthand" run)
    run_cli_expect_success("sloppy run bytes src/main.ts" "${bytes_dir}" "" run src/main.ts)
    if(NOT RUN_CLI_STDOUT STREQUAL "AB")
        message(FATAL_ERROR "sloppy run Program bytes output was not exact\nstdout:\n${RUN_CLI_STDOUT}")
    endif()
else()
    run_cli_expect_failure("non-V8 sloppy run src/main.ts" "${simple_dir}" "requires V8-enabled build" run src/main.ts)
    run_cli_expect_failure("non-V8 sloppy run .sloppy" "${simple_dir}" "requires V8-enabled build" run .sloppy)
    run_cli_expect_failure("non-V8 sloppy run fallback .sloppy" "${simple_dir}" "requires V8-enabled build" run)
endif()

run_cli_expect_success("capability program build" "${capability_dir}" "" build src/main.ts)
run_cli_expect_success("sloppy routes .sloppy json" "${capability_dir}" "\"kind\": \"program\"" routes .sloppy --format json)
if(NOT RUN_CLI_STDOUT MATCHES "\"routes\"[ \t\r\n]*:[ \t\r\n]*\\[[ \t\r\n]*\\]")
    message(FATAL_ERROR "sloppy routes .sloppy json did not report empty Program routes\nstdout:\n${RUN_CLI_STDOUT}")
endif()
run_cli_expect_success("sloppy routes .sloppy text" "${capability_dir}" "Program Plan: no route metadata expected" routes .sloppy)
run_cli_expect_failure("sloppy openapi .sloppy" "${capability_dir}" "OpenAPI is only available for web Plans with route metadata" openapi .sloppy)
run_cli_expect_success("sloppy capabilities .sloppy" "${capability_dir}" "\"token\": \"fs\"" capabilities .sloppy --format json)
run_cli_expect_success("sloppy doctor .sloppy" "${capability_dir}" "program Plan has no route metadata by design" doctor .sloppy)
run_cli_expect_success("sloppy audit .sloppy" "${capability_dir}" "\"findings\"" audit .sloppy --format json)
