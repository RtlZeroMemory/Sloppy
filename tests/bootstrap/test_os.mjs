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
            processStart(command, args, options) {
                let disposed = false;
                let stdoutReads = 0;
                return {
                    command,
                    args,
                    options,
                    readStdout(maxBytes) {
                        assert.equal(maxBytes, 16);
                        stdoutReads += 1;
                        return disposed || stdoutReads > 2 ? "" : "alpha\nbeta\n";
                    },
                    readStderr() {
                        return "";
                    },
                    writeStdin(value) {
                        assert.equal(value, "input");
                        return value.length;
                    },
                    closeStdin() {
                        return undefined;
                    },
                    wait(waitOptions) {
                        return { exitCode: 0, timedOut: waitOptions.timeoutMs === 1 };
                    },
                    terminate() {
                        return { killed: true };
                    },
                    kill() {
                        return { killed: true };
                    },
                    cancel() {
                        return { cancelled: true };
                    },
                    dispose() {
                        disposed = true;
                    },
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

    const proc = await Process.start("tool", ["arg"], { stdin: "pipe", stdout: "pipe", stderr: "pipe" });
    assert.deepEqual(proc._handle.command, "tool");
    assert.deepEqual(proc._handle.args, ["arg"]);
    assert.deepEqual(proc._handle.options, { stdin: "pipe", stdout: "pipe", stderr: "pipe" });
    assert.equal(await proc.stdin.writeText("input"), 5);
    assert.equal(await proc.stdout.readText(16), "alpha\nbeta\n");
    const lines = [];
    for await (const line of proc.stdout.readLines({ chunkSize: 16 })) {
        lines.push(line);
    }
    assert.deepEqual(lines, ["alpha", "beta"]);
    assert.deepEqual(await proc.wait({ timeoutMs: 1 }), { exitCode: 0, timedOut: true });
    await assert.rejects(proc.wait({ deadline: { remainingMs: () => Number.NaN } }), TypeError);
    assert.deepEqual(await proc.kill(), { killed: true });
    assert.deepEqual(await proc.cancel(), { cancelled: true });
    await proc.dispose();

    await assertOsRejects(
        Process.start("tool", [], { deadline: { remainingMs: () => 0 } }),
        "SLOPPY_E_OS_PROCESS_TIMEOUT",
    );
    await assertOsRejects(
        Process.start("tool", [], { signal: { aborted: true, reason: "cancelled" } }),
        "SLOPPY_E_OS_PROCESS_CANCELLED",
    );
    await assert.rejects(Process.start("tool", [], { stdout: "inherit" }), TypeError);
} finally {
    if (previousSloppy === undefined) {
        delete globalThis.__sloppy;
    } else {
        globalThis.__sloppy = previousSloppy;
    }
}
