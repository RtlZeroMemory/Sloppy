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
assertOsError(() => Process.info(), "SLOPPY_E_OS_FEATURE_UNAVAILABLE");
await assertOsRejects(Process.run("echo", []), "SLOPPY_E_OS_FEATURE_UNAVAILABLE");
assert.throws(() => Signals.onShutdown("not-a-function"), TypeError);
assertOsError(() => Signals.onShutdown(() => {}), "SLOPPY_E_OS_FEATURE_UNAVAILABLE");

const previousSloppy = globalThis.__sloppy;
try {
    const bridgeProcesses = new Map();
    let nextProcessSlot = 1;

    function processKey(handle) {
        assert.equal(typeof handle?.slot, "number");
        assert.equal(typeof handle?.generation, "number");
        return `${handle.slot}:${handle.generation}`;
    }

    function processState(handle) {
        const state = bridgeProcesses.get(processKey(handle));
        assert.notEqual(state, undefined);
        return state;
    }

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
            processInfo() {
                return {
                    pid: 123,
                    parentPid: 45,
                    executablePath: "/bin/sloppy",
                    cwd: "/work/app",
                    args: ["sloppy", "run", "app.js"],
                    argsAvailable: true,
                };
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
                const handle = Object.freeze({ slot: nextProcessSlot++, generation: 1 });
                bridgeProcesses.set(processKey(handle), {
                    command,
                    args,
                    options,
                    disposed: false,
                    stdoutReads: 0,
                });
                return handle;
            },
            processReadStdout(handle, maxBytes) {
                const state = processState(handle);
                assert.equal(maxBytes, 16);
                state.stdoutReads += 1;
                return state.disposed || state.stdoutReads > 2 ? "" : "alpha\nbeta\n";
            },
            processReadStderr() {
                return "";
            },
            processWriteStdin(handle, value) {
                processState(handle);
                assert.equal(value, "input");
                return value.length;
            },
            processCloseStdin(handle) {
                processState(handle);
                return undefined;
            },
            processWait(handle, waitOptions) {
                processState(handle);
                return { exitCode: 0, timedOut: waitOptions.timeoutMs === 1 };
            },
            processTerminate(handle) {
                processState(handle);
                return { killed: true };
            },
            processKill(handle) {
                processState(handle);
                return { killed: true };
            },
            processCancel(handle) {
                processState(handle);
                return { cancelled: true };
            },
            processDispose(handle) {
                const state = processState(handle);
                state.disposed = true;
                bridgeProcesses.delete(processKey(handle));
            },
            signalsOnShutdown(handler) {
                this.shutdownHandler = handler;
                return {
                    dispose() {
                        globalThis.__sloppy.os.shutdownHandler = undefined;
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
    const info = Process.info();
    assert.deepEqual(info, {
        pid: 123,
        parentPid: 45,
        executablePath: "/bin/sloppy",
        cwd: "/work/app",
        args: ["sloppy", "run", "app.js"],
        argsAvailable: true,
    });
    assert.equal(Object.isFrozen(info), true);
    assert.equal(Object.isFrozen(info.args), true);
    globalThis.__sloppy.os.processInfo = () => ({
        pid: 123,
        parentPid: 45,
        executablePath: "/bin/sloppy",
        cwd: "/work/app",
        args: [],
        argsAvailable: false,
    });
    assert.deepEqual(Process.info(), {
        pid: 123,
        parentPid: 45,
        executablePath: "/bin/sloppy",
        cwd: "/work/app",
        args: [],
        argsAvailable: false,
    });
    globalThis.__sloppy.os.processInfo = () => ({
        pid: -1,
        parentPid: 0,
        executablePath: "",
        cwd: "",
        args: [],
        argsAvailable: true,
    });
    assertOsError(() => Process.info(), "SLOPPY_E_OS_FEATURE_UNAVAILABLE");
    globalThis.__sloppy.os.processInfo = () => ({
        pid: 1,
        parentPid: 0,
        executablePath: "",
        cwd: "",
        args: ["hidden"],
        argsAvailable: false,
    });
    assertOsError(() => Process.info(), "SLOPPY_E_OS_FEATURE_UNAVAILABLE");
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
    assert.deepEqual(proc._handle, { slot: 1, generation: 1 });
    assert.equal(processState(proc._handle).command, "tool");
    assert.deepEqual(processState(proc._handle).args, ["arg"]);
    assert.deepEqual(processState(proc._handle).options, { stdin: "pipe", stdout: "pipe", stderr: "pipe" });
    await assert.rejects(proc.stdin.write(42), TypeError);
    assert.equal(await proc.stdin.writeText("input"), 5);
    assert.equal(await proc.stdout.readText(16), "alpha\nbeta\n");
    await assert.rejects(async () => {
        for await (const _line of proc.stdout.readLines("bad")) {
            // unreachable
        }
    }, TypeError);
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

    const shutdownEvents = [];
    const registration = Signals.onShutdown(async (ctx) => {
        shutdownEvents.push(ctx);
    });
    await globalThis.__sloppy.os.shutdownHandler({ signal: "SIGTERM", forced: true, reason: "test" });
    assert.deepEqual(shutdownEvents, [Object.freeze({ signal: "SIGTERM", forced: true, reason: "test" })]);
    registration.dispose();
    assert.equal(globalThis.__sloppy.os.shutdownHandler, undefined);

    Signals.onShutdown(() => {
        throw new Error("boom");
    });
    await assertOsRejects(
        globalThis.__sloppy.os.shutdownHandler({ signal: "SIGINT" }),
        "SLOPPY_E_OS_SIGNAL_HANDLER_FAILURE",
    );
} finally {
    if (previousSloppy === undefined) {
        delete globalThis.__sloppy;
    } else {
        globalThis.__sloppy = previousSloppy;
    }
}
