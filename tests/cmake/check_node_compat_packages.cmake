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

find_program(SLOPPY_NODE_COMPAT_NODE NAMES node.exe node)
if(NOT SLOPPY_NODE_COMPAT_NODE AND EXISTS "C:/Program Files/nodejs/node.exe")
    set(SLOPPY_NODE_COMPAT_NODE "C:/Program Files/nodejs/node.exe")
endif()
find_program(SLOPPY_NODE_COMPAT_NPM NAMES npm.cmd npm)
if(NOT SLOPPY_NODE_COMPAT_NPM AND EXISTS "C:/Program Files/nodejs/npm.cmd")
    set(SLOPPY_NODE_COMPAT_NPM "C:/Program Files/nodejs/npm.cmd")
endif()
set(SLOPPY_NODE_COMPAT_NPM_CLI "")
if(DEFINED ENV{APPDATA})
    set(SLOPPY_NODE_COMPAT_NPM_CLI_CANDIDATE "$ENV{APPDATA}/npm/node_modules/npm/bin/npm-cli.js")
    if(EXISTS "${SLOPPY_NODE_COMPAT_NPM_CLI_CANDIDATE}")
        set(SLOPPY_NODE_COMPAT_NPM_CLI "${SLOPPY_NODE_COMPAT_NPM_CLI_CANDIDATE}")
    endif()
endif()
if(NOT SLOPPY_NODE_COMPAT_NPM_CLI AND EXISTS "C:/Program Files/nodejs/node_modules/npm/bin/npm-cli.js")
    set(SLOPPY_NODE_COMPAT_NPM_CLI "C:/Program Files/nodejs/node_modules/npm/bin/npm-cli.js")
endif()
if(NOT SLOPPY_NODE_COMPAT_NPM_CLI AND SLOPPY_NODE_COMPAT_NPM)
    execute_process(
        COMMAND "${SLOPPY_NODE_COMPAT_NPM}" root -g
        TIMEOUT 30
        RESULT_VARIABLE SLOPPY_NODE_COMPAT_NPM_ROOT_RESULT
        OUTPUT_VARIABLE SLOPPY_NODE_COMPAT_NPM_ROOT
        ERROR_QUIET)
    if(SLOPPY_NODE_COMPAT_NPM_ROOT_RESULT EQUAL 0)
        string(STRIP "${SLOPPY_NODE_COMPAT_NPM_ROOT}" SLOPPY_NODE_COMPAT_NPM_ROOT)
        set(SLOPPY_NODE_COMPAT_NPM_CLI_CANDIDATE "${SLOPPY_NODE_COMPAT_NPM_ROOT}/npm/bin/npm-cli.js")
        if(EXISTS "${SLOPPY_NODE_COMPAT_NPM_CLI_CANDIDATE}")
            set(SLOPPY_NODE_COMPAT_NPM_CLI "${SLOPPY_NODE_COMPAT_NPM_CLI_CANDIDATE}")
        endif()
    endif()
endif()
if(NOT SLOPPY_NODE_COMPAT_NODE OR NOT SLOPPY_NODE_COMPAT_NPM_CLI)
    message(FATAL_ERROR "node compatibility package fixtures require npm to install file dependencies")
endif()

set(work_dir "${CMAKE_BINARY_DIR}/node-compat-packages")
set(project_dir "${work_dir}/project")
set(outside_dir "${work_dir}/outside")
file(REMOVE_RECURSE "${work_dir}")
file(MAKE_DIRECTORY "${project_dir}/src")
file(MAKE_DIRECTORY "${outside_dir}")

function(write_package_json package_name body)
    file(MAKE_DIRECTORY "${project_dir}/fixtures/${package_name}")
    file(WRITE "${project_dir}/fixtures/${package_name}/package.json" "${body}")
endfunction()

write_package_json("package-process-buffer" [=[{
  "name": "package-process-buffer",
  "version": "1.0.0",
  "type": "module",
  "exports": "./index.js"
}
]=])
file(WRITE "${project_dir}/fixtures/package-process-buffer/index.js" [=[
import process from "node:process";
import { Buffer } from "node:buffer";

export function checkProcessBuffer() {
  process.exitCode = 0;
  process.env.SLOPPY_NODE_COMPAT_FIXTURE = "ok";
  const euro = Buffer.from("e282ac", "hex").toString("utf8");
  const joined = Buffer.concat([Buffer.from("sl"), Buffer.from("oppy")]).toString();
  const encoded = Buffer.from("c2VhbGVk", "base64").toString("utf8");
  const aliasSource = Buffer.from("prefix");
  const alias = aliasSource.subarray(0, 3);
  alias[0] = 80;
  const slice = `${alias.toString()}:${aliasSource.toString()}`;
  return `${process.platform}:${process.arch}:${typeof process.cwd()}:${process.version}:${process.exitCode}:${process.env.SLOPPY_NODE_COMPAT_FIXTURE}:${"SLOPPY_NODE_COMPAT_FIXTURE" in process.env}:${euro}:${joined}:${encoded}:${slice}`;
}
]=])

write_package_json("package-fs-promises" [=[{
  "name": "package-fs-promises",
  "version": "1.0.0",
  "type": "module",
  "exports": "./index.js"
}
]=])
file(WRITE "${project_dir}/fixtures/package-fs-promises/index.js" [=[
import { access, appendFile, mkdir, readFile, readdir, rm, stat, writeFile } from "node:fs/promises";

export async function checkFsPromises() {
  const dir = ".node-compat-fixture-fs";
  const file = `${dir}/message.txt`;
  await mkdir(dir, { recursive: true });
  await writeFile(file, "hello", "utf8");
  await appendFile(file, " fs", "utf8");
  await access(file);
  const text = await readFile(file, "utf8");
  const bytesFile = `${dir}/bytes.bin`;
  await writeFile(bytesFile, new Uint8Array([1, 2, 3]), "utf8");
  await appendFile(bytesFile, new Uint8Array([4]), "utf8");
  const bytes = await readFile(bytesFile);
  const names = await readdir(dir);
  const info = await stat(file);
  let rejected = false;
  try {
    await readFile(file, "latin1");
  } catch {
    rejected = true;
  }
  await rm(dir, { recursive: true });
  return `${text}:${names.includes("message.txt")}:${info.isFile() === true}:${rejected}:${Array.from(bytes).join(",")}`;
}
]=])

write_package_json("package-assert" [=[{
  "name": "package-assert",
  "version": "1.0.0",
  "type": "module",
  "exports": "./index.js"
}
]=])
file(WRITE "${project_dir}/fixtures/package-assert/index.js" [=[
import assert from "node:assert";
import strictAssert from "node:assert/strict";

export async function checkAssert() {
  assert.ok(true);
  assert.equal("1", 1);
  assert.strictEqual(2, 2);
  assert.deepStrictEqual({ b: [2], a: 1 }, { a: 1, b: [2] });
  assert.throws(() => assert.ok(false), assert.AssertionError);
  assert.throws(() => assert.throws(() => { throw new Error("wrong"); }, TypeError), assert.AssertionError);
  assert.throws(() => assert.throws(() => { throw new Error("wrong"); }, () => false), assert.AssertionError);
  assert.throws(() => strictAssert.equal(1, "1"), assert.AssertionError);
  await assert.rejects(Promise.reject(new Error("boom")), /boom/);
  return "assert";
}
]=])

write_package_json("package-stream-basic" [=[{
  "name": "package-stream-basic",
  "version": "1.0.0",
  "type": "module",
  "exports": "./index.js"
}
]=])
file(WRITE "${project_dir}/fixtures/package-stream-basic/index.js" [=[
import { PassThrough, Readable, Writable } from "node:stream";
import { pipeline } from "node:stream/promises";

export async function checkStreamBasic() {
  const pass = new PassThrough();
  const seen = [];
  pass.on("data", (chunk) => seen.push(chunk));
  pass.write("a");
  for await (const chunk of Readable.from(["b", "c"])) {
    seen.push(chunk);
  }
  const piped = [];
  await pipeline(Readable.from(["d"]), new Writable({ write(chunk, _encoding, callback) { Promise.resolve().then(() => { piped.push(chunk); callback(); }); } }));
  return `${seen.join("")}:${piped.join("")}`;
}
]=])

write_package_json("package-crypto-basic" [=[{
  "name": "package-crypto-basic",
  "version": "1.0.0",
  "type": "module",
  "exports": "./index.js"
}
]=])
file(WRITE "${project_dir}/fixtures/package-crypto-basic/index.js" [=[
import crypto from "node:crypto";

export async function checkCryptoBasic() {
  const digest = await crypto.createHash("sha256").update("abc").digest("hex");
  const hmac = await crypto.createHmac("sha256", new Uint8Array([107, 101, 121])).update(new Uint8Array([97, 98, 99])).digest("hex");
  const random = crypto.randomBytes(8);
  const same = crypto.timingSafeEqual(random, random.slice(0));
  return `${digest.slice(0, 8)}:${hmac.length}:${same}`;
}
]=])

write_package_json("package-cjs-json" [=[{
  "name": "package-cjs-json",
  "version": "1.0.0",
  "main": "index.cjs"
}
]=])
file(WRITE "${project_dir}/fixtures/package-cjs-json/data.json" [=[{"name":"json-ok"}]=])
file(WRITE "${project_dir}/fixtures/package-cjs-json/index.cjs" [=[
const data = require("./data.json");
module.exports = function checkCjsJson() {
  return data.name;
};
]=])

write_package_json("package-mixed-esm-cjs" [=[{
  "name": "package-mixed-esm-cjs",
  "version": "1.0.0",
  "type": "module",
  "exports": {
    ".": "./index.js",
    "./feature": "./feature.cjs"
  }
}
]=])
file(WRITE "${project_dir}/fixtures/package-mixed-esm-cjs/common.cjs" [=[
module.exports = { value: "cjs-default", default: "shadow-default" };
]=])
file(WRITE "${project_dir}/fixtures/package-mixed-esm-cjs/index.js" [=[
import common from "./common.cjs";
export default function checkMixed() {
  return common.value;
}
]=])
file(WRITE "${project_dir}/fixtures/package-mixed-esm-cjs/feature.cjs" [=[
module.exports = function feature() {
  return "subpath";
};
]=])

file(WRITE "${project_dir}/package.json" [=[{
  "name": "node-compat-package-suite",
  "private": true,
  "type": "module",
  "dependencies": {
    "package-process-buffer": "file:fixtures/package-process-buffer",
    "package-fs-promises": "file:fixtures/package-fs-promises",
    "package-assert": "file:fixtures/package-assert",
    "package-stream-basic": "file:fixtures/package-stream-basic",
    "package-crypto-basic": "file:fixtures/package-crypto-basic",
    "package-cjs-json": "file:fixtures/package-cjs-json",
    "package-mixed-esm-cjs": "file:fixtures/package-mixed-esm-cjs"
  }
}
]=])
file(WRITE "${project_dir}/sloppy.json" [=[{
  "kind": "program",
  "entry": "src/main.ts",
  "outDir": ".sloppy"
}
]=])
file(WRITE "${project_dir}/src/main.ts" [=[
import { checkAssert } from "package-assert";
import checkCjsJson from "package-cjs-json";
import { checkCryptoBasic } from "package-crypto-basic";
import { checkFsPromises } from "package-fs-promises";
import checkMixed from "package-mixed-esm-cjs";
import feature from "package-mixed-esm-cjs/feature";
import { checkProcessBuffer } from "package-process-buffer";
import { checkStreamBasic } from "package-stream-basic";

export async function main() {
  const results = [
    await checkAssert(),
    checkCjsJson(),
    await checkCryptoBasic(),
    await checkFsPromises(),
    checkMixed(),
    feature(),
    checkProcessBuffer(),
    await checkStreamBasic()
  ];
  console.log(JSON.stringify({ results }));
}
]=])

execute_process(
    COMMAND "${SLOPPY_NODE_COMPAT_NODE}" "${SLOPPY_NODE_COMPAT_NPM_CLI}" install --ignore-scripts --no-audit
    WORKING_DIRECTORY "${project_dir}"
    TIMEOUT 120
    RESULT_VARIABLE npm_result
    OUTPUT_VARIABLE npm_stdout
    ERROR_VARIABLE npm_stderr)
if(NOT npm_result EQUAL 0)
    message(FATAL_ERROR "node compatibility fixture npm install failed\nstdout:\n${npm_stdout}\nstderr:\n${npm_stderr}")
endif()

function(run_cli_expect_success description working_dir expected_pattern)
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E env "SLOPPY_SLOPPYC=${SLOPPYC_EXECUTABLE}" "${SLOPPY_CLI}" ${ARGN}
        WORKING_DIRECTORY "${working_dir}"
        TIMEOUT 240
        RESULT_VARIABLE result
        OUTPUT_VARIABLE stdout
        ERROR_VARIABLE stderr)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "${description} failed\nstdout:\n${stdout}\nstderr:\n${stderr}")
    endif()
    if(expected_pattern AND NOT stdout MATCHES "${expected_pattern}")
        message(FATAL_ERROR "${description} output did not match '${expected_pattern}'\nstdout:\n${stdout}\nstderr:\n${stderr}")
    endif()
    set(RUN_CLI_STDOUT "${stdout}" PARENT_SCOPE)
    set(RUN_CLI_STDERR "${stderr}" PARENT_SCOPE)
endfunction()

function(run_cli_expect_failure description working_dir expected_pattern)
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E env "SLOPPY_SLOPPYC=${SLOPPYC_EXECUTABLE}" "${SLOPPY_CLI}" ${ARGN}
        WORKING_DIRECTORY "${working_dir}"
        TIMEOUT 120
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

run_cli_expect_success("node compatibility package suite build" "${project_dir}" "" build)
run_cli_expect_success("node compatibility package suite deps" "${project_dir}" "\"nodeBuiltins\"" deps .sloppy --format json)
foreach(expected IN ITEMS package-process-buffer package-fs-promises package-assert package-stream-basic package-crypto-basic package-cjs-json package-mixed-esm-cjs node:process node:buffer node:fs/promises node:assert node:assert/strict node:stream node:stream/promises node:crypto)
    if(NOT RUN_CLI_STDOUT MATCHES "${expected}")
        message(FATAL_ERROR "node compatibility dependency graph did not include ${expected}\nstdout:\n${RUN_CLI_STDOUT}")
    endif()
endforeach()
run_cli_expect_success("node compatibility package suite package" "${project_dir}" "Created Sloppy app package" package)
if(NOT EXISTS "${project_dir}/.sloppy/package/artifacts/deps.graph.json")
    message(FATAL_ERROR "node compatibility package did not copy deps.graph.json")
endif()

file(COPY "${project_dir}/.sloppy/package" DESTINATION "${outside_dir}")
file(REMOVE_RECURSE "${project_dir}/node_modules")
if(EXISTS "${outside_dir}/package/node_modules")
    message(FATAL_ERROR "node compatibility package unexpectedly contains node_modules")
endif()

if(SLOPPY_EXPECT_RUN_SUCCESS)
    run_cli_expect_success("node compatibility package suite run" "${project_dir}" "\"assert\"" run .sloppy)
    foreach(expected IN ITEMS json-ok ba7816bf "hello fs:true:true:true:1,2,3,4" cjs-default subpath sloppy sealed "ok:true" "Pre:Prefix" "abc:d")
        if(NOT RUN_CLI_STDOUT MATCHES "${expected}")
            message(FATAL_ERROR "node compatibility run did not include ${expected}\nstdout:\n${RUN_CLI_STDOUT}\nstderr:\n${RUN_CLI_STDERR}")
        endif()
    endforeach()
    run_cli_expect_success("node compatibility package suite outside package run" "${outside_dir}" "\"assert\"" run "${outside_dir}/package")
endif()

set(native_dir "${work_dir}/native-addon")
file(MAKE_DIRECTORY "${native_dir}/src" "${native_dir}/node_modules/native-addon")
file(WRITE "${native_dir}/sloppy.json" [=[{
  "kind": "program",
  "entry": "src/main.ts",
  "outDir": ".sloppy"
}
]=])
file(WRITE "${native_dir}/src/main.ts" [=[import native from "native-addon"; export function main() { return native; }]=])
file(WRITE "${native_dir}/node_modules/native-addon/package.json" [=[{
  "name": "native-addon",
  "version": "1.0.0",
  "main": "binding.node"
}
]=])
file(WRITE "${native_dir}/node_modules/native-addon/binding.node" "native")
run_cli_expect_failure("node compatibility native addon rejection" "${native_dir}" "SLOPPYC_E_NATIVE_ADDON_UNSUPPORTED" build)

set(unsupported_dir "${work_dir}/unsupported-builtin")
file(MAKE_DIRECTORY "${unsupported_dir}/src")
file(WRITE "${unsupported_dir}/sloppy.json" [=[{
  "kind": "program",
  "entry": "src/main.ts",
  "outDir": ".sloppy"
}
]=])
file(WRITE "${unsupported_dir}/src/main.ts" [=[import cp from "node:child_process"; export function main() { return cp; }]=])
run_cli_expect_failure("node compatibility unsupported builtin rejection" "${unsupported_dir}" "SLOPPYC_E_UNSUPPORTED_NODE_BUILTIN" build)
