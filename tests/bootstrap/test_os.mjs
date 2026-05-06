import assert from "node:assert/strict";

import { Environment, Process, Signals, System } from "../../stdlib/sloppy/index.js";

function assertOsError(fn, code) {
    assert.throws(fn, (error) => {
        assert.equal(error.code, code);
        assert.match(error.message, new RegExp(code));
        return true;
    });
}

async function assertOsRejects(promise, code) {
    await assert.rejects(promise, (error) => {
        assert.equal(error.code, code);
        assert.match(error.message, new RegExp(code));
        return true;
    });
}

assertOsError(() => System.platform, "SLOPPY_E_OS_FEATURE_UNAVAILABLE");
assert.throws(() => Environment.get(""), TypeError);
assert.throws(() => Environment.has("A=B"), TypeError);
assert.throws(() => Environment.list({ values: true }), TypeError);
await assertOsRejects(Process.run("echo", []), "SLOPPY_E_OS_FEATURE_UNAVAILABLE");
assertOsError(() => Signals.onShutdown(() => {}), "SLOPPY_E_OS_FEATURE_UNAVAILABLE");

const previousSloppy = globalThis.__sloppy;
try {
    globalThis.__sloppy = {
        ...(previousSloppy ?? {}),
        os: {
            systemInfo() {
                return {
                    platform: "test-platform",
                    arch: "test-arch",
                    cpuCount: 8,
                    tempDirectory: "/tmp/sloppy",
                    hostname: "test-host",
                    endOfLine: "\n",
                };
            },
            environmentGet(key) {
                return key === "SLOPPY_OS_TEST" ? "visible" : undefined;
            },
            environmentHas(key) {
                return key === "SLOPPY_OS_TEST";
            },
            environmentList(prefix) {
                return ["SLOPPY_OS_TEST", "SLOPPY_OS_SECRET_TOKEN"].filter((key) => key.startsWith(prefix));
            },
            processRun(command, args, options) {
                return {
                    command,
                    args,
                    options,
                    exitCode: 0,
                    stdout: "ok",
                    stderr: "",
                    timedOut: false,
                };
            },
        },
    };

    assert.equal(System.platform, "test-platform");
    assert.equal(System.arch, "test-arch");
    assert.equal(System.cpuCount, 8);
    assert.equal(System.tempDirectory, "/tmp/sloppy");
    assert.equal(System.hostname, "test-host");
    assert.equal(System.endOfLine, "\n");
    assert.equal(Environment.get("SLOPPY_OS_TEST"), "visible");
    assert.equal(Environment.get("SLOPPY_OS_MISSING"), undefined);
    assert.equal(Environment.has("SLOPPY_OS_TEST"), true);
    assert.deepEqual(Environment.list({ prefix: "SLOPPY_OS_" }), [
        "SLOPPY_OS_TEST",
        "SLOPPY_OS_SECRET_TOKEN",
    ]);
    assert.deepEqual(await Process.run("tool", ["arg"], { timeoutMs: 5, capture: "text" }), {
        command: "tool",
        args: ["arg"],
        options: {
            capture: "text",
            maxStdoutBytes: 65536,
            maxStderrBytes: 65536,
            timeoutMs: 5,
        },
        exitCode: 0,
        stdout: "ok",
        stderr: "",
        timedOut: false,
    });
    assert.deepEqual(await Process.run("tool", [], { deadline: { remainingMs: () => Infinity }, signal: { aborted: false, reason: undefined } }), {
        command: "tool",
        args: [],
        options: {
            capture: "text",
            maxStdoutBytes: 65536,
            maxStderrBytes: 65536,
            timeoutMs: 0,
        },
        exitCode: 0,
        stdout: "ok",
        stderr: "",
        timedOut: false,
    });
    await assertOsRejects(
        Process.run("tool", [], { deadline: { remainingMs: () => 0 } }),
        "SLOPPY_E_OS_PROCESS_TIMEOUT",
    );
    await assertOsRejects(
        Process.run("tool", [], { signal: { aborted: true, reason: "cancelled" } }),
        "SLOPPY_E_OS_PROCESS_CANCELLED",
    );
    await assert.rejects(Process.run("tool", [], { signal: {} }), TypeError);
    await assert.rejects(Process.run("tool", ["bad\0arg"]), TypeError);
} finally {
    if (previousSloppy === undefined) {
        delete globalThis.__sloppy;
    } else {
        globalThis.__sloppy = previousSloppy;
    }
}
