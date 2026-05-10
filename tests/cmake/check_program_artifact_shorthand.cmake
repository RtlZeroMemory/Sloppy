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
set(console_dir "${work_dir}/console")
set(named_main_dir "${work_dir}/named-main")
set(default_dir "${work_dir}/default")
set(top_level_dir "${work_dir}/top-level")
set(exit_dir "${work_dir}/exit")
set(invalid_exit_dir "${work_dir}/invalid-exit")
set(throw_dir "${work_dir}/throw")
set(outside_dir "${work_dir}/outside")
set(dependency_dir "${work_dir}/dependency")
set(outside_dependency_dir "${work_dir}/outside-dependency")
set(capability_dir "${work_dir}/capability")
set(bytes_dir "${work_dir}/bytes")
set(web_dir "${work_dir}/web")
file(REMOVE_RECURSE "${work_dir}")
file(MAKE_DIRECTORY "${simple_dir}/src")
file(MAKE_DIRECTORY "${console_dir}/src")
file(MAKE_DIRECTORY "${named_main_dir}/src")
file(MAKE_DIRECTORY "${default_dir}/src")
file(MAKE_DIRECTORY "${top_level_dir}/src")
file(MAKE_DIRECTORY "${exit_dir}/src")
file(MAKE_DIRECTORY "${invalid_exit_dir}/src")
file(MAKE_DIRECTORY "${throw_dir}/src")
file(MAKE_DIRECTORY "${outside_dir}")
file(MAKE_DIRECTORY "${dependency_dir}/src")
file(MAKE_DIRECTORY "${dependency_dir}/public")
file(MAKE_DIRECTORY "${dependency_dir}/node_modules/graph-helper")
file(MAKE_DIRECTORY "${outside_dependency_dir}")
file(MAKE_DIRECTORY "${capability_dir}/src")
file(MAKE_DIRECTORY "${bytes_dir}/src")
file(MAKE_DIRECTORY "${web_dir}/src")

file(WRITE "${simple_dir}/src/message.ts" "export const message: string = \"hello from program shorthand\";\n")
file(WRITE "${simple_dir}/src/main.ts" [=[
import { message } from "./message";

type ProgramResult = string;

export async function main(): Promise<ProgramResult> {
  return message;
}
]=])

file(WRITE "${console_dir}/sloppy.json" [=[{
  "kind": "program",
  "entry": "src/main.ts",
  "outDir": ".sloppy"
}
]=])
file(WRITE "${console_dir}/src/main.ts" [=[
export function main(args, ctx) {
  if (typeof console.assert !== "function") {
    throw new Error("console.assert missing");
  }
  console.assert(true);
  console.log(`args=${args.join("|")}`);
  console.info(`kind=${ctx.kind}`);
  console.debug(`cwd=${ctx.cwd.length > 0 ? "set" : "missing"}`);
  console.error("err-channel");
  console.error(new Error("err-object"));
  return 0;
}
]=])

file(WRITE "${named_main_dir}/src/main.ts" [=[
export default function(args) {
  console.log(`default=${args[0]}`);
  return 0;
}

export function main(args) {
  console.log(`main=${args[0]}`);
  return 0;
}
]=])

file(WRITE "${default_dir}/src/main.ts" [=[
export default function(args, ctx) {
  console.log(`default=${args[0]}`);
  console.log(`plan=${ctx.plan.kind}/${ctx.plan.metadataCompleteness}`);
}
]=])

file(WRITE "${top_level_dir}/src/main.ts" [=[
console.log("top-level program ran");
]=])

file(WRITE "${exit_dir}/src/main.ts" [=[
export function main() {
  return 7;
}
]=])

file(WRITE "${invalid_exit_dir}/src/main.ts" [=[
export function main() {
  return 256;
}
]=])

file(WRITE "${throw_dir}/src/main.ts" [=[
export async function main() {
  console.log("before boom");
  throw new Error("program boom");
}
]=])

file(WRITE "${dependency_dir}/sloppy.json" [=[{
  "kind": "program",
  "entry": "src/main.ts",
  "outDir": ".sloppy",
  "assetInclude": ["public/message.txt"]
}
]=])
file(WRITE "${dependency_dir}/node_modules/graph-helper/package.json" [=[{
  "name": "graph-helper",
  "version": "1.0.0",
  "type": "module",
  "exports": "./index.js"
}
]=])
file(WRITE "${dependency_dir}/node_modules/graph-helper/index.js" [=[
export function message(assetPath) {
  return `graph-helper saw ${assetPath}`;
}
]=])
file(WRITE "${dependency_dir}/public/message.txt" "hello from dependency package\n")
file(WRITE "${dependency_dir}/src/main.ts" [=[
import { message } from "graph-helper";

export function main() {
  console.log(message("public/message.txt"));
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

file(WRITE "${web_dir}/sloppy.json" [=[{
  "kind": "web",
  "entry": "src/main.ts",
  "outDir": ".sloppy"
}
]=])
file(WRITE "${web_dir}/src/main.ts" [=[
import { Sloppy, Results } from "sloppy";

const app = Sloppy.create();
app.get("/", () => Results.text("ok"));

export default app;
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

function(run_cli_expect_exit description working_dir expected_exit expected_stdout_pattern expected_stderr_pattern)
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E env "SLOPPY_SLOPPYC=${SLOPPYC_EXECUTABLE}" "${SLOPPY_CLI}"
                ${ARGN}
        WORKING_DIRECTORY "${working_dir}"
        TIMEOUT 180
        RESULT_VARIABLE result
        OUTPUT_VARIABLE stdout
        ERROR_VARIABLE stderr)
    if(NOT result EQUAL expected_exit)
        message(FATAL_ERROR "${description} exited ${result}, expected ${expected_exit}\nstdout:\n${stdout}\nstderr:\n${stderr}")
    endif()
    if(expected_stdout_pattern)
        string(REGEX MATCH "${expected_stdout_pattern}" matched_stdout "${stdout}")
        if(NOT matched_stdout)
            message(FATAL_ERROR "${description} stdout did not match '${expected_stdout_pattern}'\nstdout:\n${stdout}\nstderr:\n${stderr}")
        endif()
    endif()
    if(expected_stderr_pattern)
        string(REGEX MATCH "${expected_stderr_pattern}" matched_stderr "${stderr}")
        if(NOT matched_stderr)
            message(FATAL_ERROR "${description} stderr did not match '${expected_stderr_pattern}'\nstdout:\n${stdout}\nstderr:\n${stderr}")
        endif()
    endif()
    set(RUN_CLI_STDOUT "${stdout}" PARENT_SCOPE)
    set(RUN_CLI_STDERR "${stderr}" PARENT_SCOPE)
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
file(READ "${simple_dir}/.sloppy/package/manifest.json" package_manifest)
if(NOT package_manifest MATCHES "\"kind\": \"program\"")
    message(FATAL_ERROR "sloppy package manifest did not record program kind\n${package_manifest}")
endif()
foreach(required_manifest_entry IN ITEMS "\"plan\": \"artifacts/app.plan.json\"" "\"bundle\": \"artifacts/app.js\"" "\"sourceMap\": \"artifacts/app.js.map\"")
    if(NOT package_manifest MATCHES "${required_manifest_entry}")
        message(FATAL_ERROR "sloppy package manifest is missing ${required_manifest_entry}\n${package_manifest}")
    endif()
endforeach()

run_cli_expect_success("sloppy build program project" "${console_dir}" "" build)
run_cli_expect_success("sloppy build web project" "${web_dir}" "" build)

if(SLOPPY_EXPECT_RUN_SUCCESS)
    run_cli_expect_success("sloppy run src/main.ts" "${simple_dir}" "hello from program shorthand" run src/main.ts)
    run_cli_expect_success("sloppy run .sloppy" "${simple_dir}" "hello from program shorthand" run .sloppy)
    run_cli_expect_success("sloppy run fallback .sloppy" "${simple_dir}" "hello from program shorthand" run)
    run_cli_expect_success("sloppy run program args source" "${console_dir}" "args=--name\\|Ada\\|--verbose" run src/main.ts -- --name Ada --verbose)
    if(NOT RUN_CLI_STDOUT MATCHES "kind=program" OR NOT RUN_CLI_STDOUT MATCHES "cwd=set")
        message(FATAL_ERROR "sloppy run did not pass Program context\nstdout:\n${RUN_CLI_STDOUT}\nstderr:\n${RUN_CLI_STDERR}")
    endif()
    if(NOT RUN_CLI_STDERR MATCHES "err-channel")
        message(FATAL_ERROR "sloppy run did not route console.error to stderr\nstdout:\n${RUN_CLI_STDOUT}\nstderr:\n${RUN_CLI_STDERR}")
    endif()
    if(NOT RUN_CLI_STDERR MATCHES "err-object")
        message(FATAL_ERROR "sloppy run did not preserve Error details in console.error\nstdout:\n${RUN_CLI_STDOUT}\nstderr:\n${RUN_CLI_STDERR}")
    endif()
    run_cli_expect_success("sloppy run program args artifacts" "${console_dir}" "args=hello\\|artifact" run --artifacts .sloppy -- hello artifact)
    run_cli_expect_success("sloppy run program project" "${console_dir}" "args=project" run -- project)
    run_cli_expect_success("sloppy run named main precedence" "${named_main_dir}" "main=winner" run src/main.ts -- winner)
    if(RUN_CLI_STDOUT MATCHES "default=winner")
        message(FATAL_ERROR "sloppy run invoked default export even though named main exists\nstdout:\n${RUN_CLI_STDOUT}\nstderr:\n${RUN_CLI_STDERR}")
    endif()
    run_cli_expect_success("sloppy run default export" "${default_dir}" "default=delta" run src/main.ts -- delta)
    if(NOT RUN_CLI_STDOUT MATCHES "plan=program/opaque")
        message(FATAL_ERROR "sloppy run default export did not receive Program Plan context\nstdout:\n${RUN_CLI_STDOUT}\nstderr:\n${RUN_CLI_STDERR}")
    endif()
    run_cli_expect_success("sloppy run top-level program" "${top_level_dir}" "top-level program ran" run src/main.ts)
    run_cli_expect_exit("sloppy run numeric exit" "${exit_dir}" 7 "" "" run src/main.ts)
    run_cli_expect_exit("sloppy run invalid numeric exit" "${invalid_exit_dir}" 1 "" "exit code must be an integer from 0 to 255" run src/main.ts)
    run_cli_expect_exit("sloppy run thrown program" "${throw_dir}" 1 "before boom" "program boom" run src/main.ts)
    run_cli_expect_success("sloppy package console program" "${console_dir}" "Created Sloppy app package" package)
    run_cli_expect_success("sloppy run package directory outside checkout" "${outside_dir}" "args=packaged\\|run" run "${console_dir}/.sloppy/package" -- packaged run)
    run_cli_expect_success("sloppy build dependency graph package" "${dependency_dir}" "" build)
    run_cli_expect_success("sloppy deps .sloppy text" "${dependency_dir}" "Dependency graph: 1 package\\(s\\), 2 module\\(s\\), 1 asset\\(s\\)" deps .sloppy)
    run_cli_expect_success("sloppy deps .sloppy json" "${dependency_dir}" "\"resolvedImports\"" deps .sloppy --format json)
    if(NOT RUN_CLI_STDOUT MATCHES "\"resolver\"" OR NOT RUN_CLI_STDOUT MATCHES "\"includedBy\"")
        message(FATAL_ERROR "sloppy deps .sloppy json did not include full graph fields\nstdout:\n${RUN_CLI_STDOUT}\nstderr:\n${RUN_CLI_STDERR}")
    endif()
    run_cli_expect_success("sloppy deps --plan app.plan.json" "${dependency_dir}" "\"schemaVersion\"" deps --plan .sloppy/app.plan.json --format json)
    run_cli_expect_success("sloppy package dependency graph package" "${dependency_dir}" "Created Sloppy app package" package)
    file(COPY "${dependency_dir}/.sloppy/package" DESTINATION "${outside_dependency_dir}")
    file(REMOVE_RECURSE "${dependency_dir}")
    if(EXISTS "${outside_dependency_dir}/package/node_modules")
        message(FATAL_ERROR "packaged dependency graph artifact unexpectedly contains node_modules")
    endif()
    run_cli_expect_success("sloppy run dependency package outside checkout" "${outside_dependency_dir}" "graph-helper saw public/message.txt" run "${outside_dependency_dir}/package")
    run_cli_expect_success("sloppy run bytes src/main.ts" "${bytes_dir}" "" run src/main.ts)
    if(NOT RUN_CLI_STDOUT STREQUAL "AB")
        message(FATAL_ERROR "sloppy run Program bytes output was not exact\nstdout:\n${RUN_CLI_STDOUT}")
    endif()
    run_cli_expect_failure("sloppy run web plan with program args" "${web_dir}" "program arguments after -- are only valid for Program Plans" run .sloppy -- one two)
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
