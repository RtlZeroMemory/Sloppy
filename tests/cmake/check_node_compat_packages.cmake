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
  let writeUnsupported = false;
  try {
    process.stdout.write("x");
  } catch (error) {
    writeUnsupported = /SLOPPY_E_PROCESS_STREAM_WRITE_UNSUPPORTED/.test(error.message);
  }
  let eventSeen = false;
  const mark = () => {
    eventSeen = true;
  };
  process.addListener("fixture", mark);
  process.emit("fixture");
  process.removeListener("fixture", mark);
  const hr = process.hrtime();
  const hrBigint = typeof process.hrtime.bigint() === "bigint";
  return `${process.platform}:${process.arch}:${typeof process.cwd()}:${process.version}:${process.exitCode}:${process.env.SLOPPY_NODE_COMPAT_FIXTURE}:${"SLOPPY_NODE_COMPAT_FIXTURE" in process.env}:${euro}:${joined}:${encoded}:${slice}:${writeUnsupported}:${eventSeen}:${hr.length}:${hrBigint}`;
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
import { mkdir as mkdirCallback } from "node:fs";
import { access, appendFile, lstat, mkdir, mkdtemp, readFile, readdir, realpath, rm, stat, writeFile } from "node:fs/promises";

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
  let redacted = false;
  try {
    await access(`${dir}/missing-secret-name.txt`);
  } catch (error) {
    redacted = error.message === "SLOPPY_E_NODE_FS_ACCESS: path is not accessible.";
  }
  const callbackError = await new Promise((resolve) => {
    mkdirCallback(`${dir}/bad-callback`, { recursive: "yes" }, (error) => {
      resolve(error instanceof TypeError && /recursive/.test(error.message));
    });
  });
  const names = await readdir(dir);
  const info = await stat(file);
  let rejected = false;
  try {
    await readFile(file, "latin1");
  } catch {
    rejected = true;
  }
  let unsupportedCount = 0;
  for (const operation of [() => lstat(file), () => mkdtemp("node-compat-"), () => realpath(file)]) {
    try {
      await operation();
    } catch (error) {
      if (/SLOPPY_E_NODE_FS_UNSUPPORTED/.test(error.message)) {
        unsupportedCount += 1;
      }
    }
  }
  await rm(dir, { recursive: true });
  return `${text}:${names.includes("message.txt")}:${info.isFile() === true}:${rejected}:${Array.from(bytes).join(",")}:${redacted}:${callbackError}:${unsupportedCount}`;
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
  assert(true);
  strictAssert(true);
  assert.ok(true);
  assert.equal("1", 1);
  assert.strictEqual(2, 2);
  assert.notStrictEqual(2, "2");
  assert.deepStrictEqual({ b: [2], a: 1 }, { a: 1, b: [2] });
  assert.throws(() => assert.ok(false), assert.AssertionError);
  assert.throws(() => assert.throws(() => { throw new Error("wrong"); }, TypeError), assert.AssertionError);
  assert.throws(() => assert.throws(() => { throw new Error("wrong"); }, () => false), assert.AssertionError);
  assert.throws(() => assert.doesNotThrow(() => { throw new TypeError("skip"); }, RangeError), TypeError);
  assert.throws(() => assert.doesNotThrow(() => { throw new TypeError("boom"); }, TypeError), assert.AssertionError);
  assert.throws(() => assert.fail("plain fail"), assert.AssertionError);
  assert.throws(() => assert.fail(1, 2, "bad", "!="), assert.AssertionError);
  assert.throws(() => strictAssert.equal(1, "1"), assert.AssertionError);
  await assert.rejects(Promise.reject(new Error("boom")), /boom/);
  await assert.rejects(assert.doesNotReject(1), TypeError);
  await assert.rejects(assert.doesNotReject(Promise.reject(new TypeError("skip")), RangeError), TypeError);
  await assert.rejects(assert.doesNotReject(Promise.reject(new TypeError("boom")), TypeError), assert.AssertionError);
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

write_package_json("package-node-builtins-more" [=[{
  "name": "package-node-builtins-more",
  "version": "1.0.0",
  "type": "module",
  "exports": {
    ".": {
      "sloppy": "./sloppy.js",
      "node": "./node.js",
      "default": "./index.js"
    },
    "./feature": "./feature.js"
  },
  "imports": {
    "#label": "./label.js"
  }
}
]=])
file(WRITE "${project_dir}/fixtures/package-node-builtins-more/label.js" [=[
export default "imports-ok";
]=])
file(WRITE "${project_dir}/fixtures/package-node-builtins-more/feature.js" [=[
export const feature = "subpath-ok";
]=])
file(WRITE "${project_dir}/fixtures/package-node-builtins-more/node.js" [=[
export default function nodeOnly() {
  return "node-condition";
}
]=])
file(WRITE "${project_dir}/fixtures/package-node-builtins-more/index.js" [=[
export default function defaultOnly() {
  return "default-condition";
}
]=])
file(WRITE "${project_dir}/fixtures/package-node-builtins-more/sloppy.js" [=[
import assert from "node:assert";
import { Buffer } from "node:buffer";
import consoleModule from "node:console";
import constants from "node:constants";
import diagnostics from "node:diagnostics_channel";
import { createRequire } from "node:module";
import { performance } from "node:perf_hooks";
import { StringDecoder } from "node:string_decoder";
import tty from "node:tty";
import http from "node:http";
import util from "node:util";
import label from "#label";
import { feature } from "./feature";

export default async function checkMoreBuiltins() {
  assert.doesNotThrow(() => assert.ifError(null));
  assert.strictEqual(Buffer.isEncoding(undefined), false);
  assert.strictEqual(typeof consoleModule.log, "function");
  assert.strictEqual(typeof consoleModule.warn, "function");
  assert.strictEqual(typeof consoleModule.info, "function");
  assert.strictEqual(typeof consoleModule.debug, "function");
  assert.strictEqual(diagnostics.hasSubscribers("unused-fixture-channel"), false);
  assert.strictEqual(util.format("%d:%i", 1.5, 1.5), "1.5:1");
  assert.strictEqual(typeof performance.timeOrigin, "number");
  const require = createRequire("node_modules/package-node-builtins-more/sloppy.js");
  const resolved = require.resolve("./feature");
  const decoder = new StringDecoder("utf8");
  const decoded = decoder.write(Buffer.from("e282", "hex")) + decoder.end(Buffer.from("ac", "hex"));
  const channel = diagnostics.channel("fixture");
  let diagnostic = "none";
  channel.subscribe((message) => {
    diagnostic = message.kind;
  });
  channel.publish({ kind: "diagnostic-ok" });
  let httpStub = false;
  try {
    const req = http.request("http://example.test/fixture", { method: "POST", headers: { "x-fixture": "ok" } });
    req.setHeader("x-extra", "1");
    httpStub = req.getHeader("x-extra") === "1" &&
      typeof req.write === "function" &&
      typeof req.end === "function" &&
      typeof http.globalAgent.destroy === "function";
    req.destroy();
  } catch {
    httpStub = false;
  }
  consoleModule.log("node-builtins-more");
  const perfOk = performance.now() >= 0 && performance.timeOrigin > 0;
  return `${global.process.browser}:${typeof global.Buffer}:${constants.O_RDONLY}:${decoded}:${diagnostic}:${perfOk}:${tty.isatty(1)}:${label}:${feature}:${resolved.includes("feature")}:${httpStub}`;
}
]=])

write_package_json("package-zlib-basic" [=[{
  "name": "package-zlib-basic",
  "version": "1.0.0",
  "type": "module",
  "exports": "./index.js"
}
]=])
file(WRITE "${project_dir}/fixtures/package-zlib-basic/index.js" [=[
import zlib from "node:zlib";
import { Buffer } from "node:buffer";

export async function checkZlibBasic() {
  const gz = await new Promise((resolve, reject) => {
    zlib.gzip(Buffer.from("zip-ok"), (error, value) => error ? reject(error) : resolve(value));
  });
  const plain = await new Promise((resolve, reject) => {
    zlib.gunzip(gz, (error, value) => error ? reject(error) : resolve(value));
  });
  let syncStub = false;
  try {
    zlib.gzipSync(Buffer.from("x"));
  } catch (error) {
    syncStub = /SLOPPY_E_NODE_ZLIB_SYNC_UNSUPPORTED/.test(error.message);
  }
  const deflateStub = await new Promise((resolve) => {
    zlib.deflate(Buffer.from("x"), (error) => {
      resolve(/SLOPPY_E_NODE_ZLIB_UNSUPPORTED/.test(error.message));
    });
  });
  return `${Buffer.from(plain).toString()}:${syncStub}:${deflateStub}`;
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
    "package-node-builtins-more": "file:fixtures/package-node-builtins-more",
    "package-zlib-basic": "file:fixtures/package-zlib-basic",
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
import checkMoreBuiltins from "package-node-builtins-more";
import { checkProcessBuffer } from "package-process-buffer";
import { checkStreamBasic } from "package-stream-basic";
import { checkZlibBasic } from "package-zlib-basic";

export async function main() {
  const results = [
    await checkAssert(),
    checkCjsJson(),
    await checkCryptoBasic(),
    await checkFsPromises(),
    checkMixed(),
    await checkMoreBuiltins(),
    feature(),
    checkProcessBuffer(),
    await checkStreamBasic(),
    await checkZlibBasic()
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
foreach(expected IN ITEMS package-process-buffer package-fs-promises package-assert package-stream-basic package-crypto-basic package-node-builtins-more package-zlib-basic package-cjs-json package-mixed-esm-cjs node:process node:buffer node:fs node:fs/promises node:assert node:assert/strict node:stream node:stream/promises node:crypto node:module node:string_decoder node:constants node:console node:perf_hooks node:diagnostics_channel node:tty node:http node:zlib)
    if(NOT RUN_CLI_STDOUT MATCHES "${expected}")
        message(FATAL_ERROR "node compatibility dependency graph did not include ${expected}\nstdout:\n${RUN_CLI_STDOUT}")
    endif()
endforeach()
run_cli_expect_success("node compatibility package suite deps explain" "${project_dir}" "Compatibility explanation" deps .sloppy --explain)
foreach(expected IN ITEMS "Packages bundled" "Node compatibility shims used" "node:zlib")
    if(NOT RUN_CLI_STDOUT MATCHES "${expected}")
        message(FATAL_ERROR "node compatibility deps explain did not include ${expected}\nstdout:\n${RUN_CLI_STDOUT}")
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
    foreach(expected IN ITEMS json-ok ba7816bf "hello fs:true:true:true:1,2,3,4:true:true:3" cjs-default subpath "false:function:0:.*:diagnostic-ok:true:false:imports-ok:subpath-ok:true:true" sloppy sealed "ok:true" "Pre:Prefix:true" "abc:d" "zip-ok:true:true")
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
