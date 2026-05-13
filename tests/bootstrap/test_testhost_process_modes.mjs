import assert from "node:assert/strict";
import { spawn } from "node:child_process";
import crypto from "node:crypto";
import fs from "node:fs/promises";
import net from "node:net";
import os from "node:os";
import path from "node:path";
import { fileURLToPath } from "node:url";

import { Text } from "../../stdlib/sloppy/codec.js";
import { TestHttp } from "../../stdlib/sloppy/http.js";
import { TestHost } from "../../stdlib/sloppy/testing.js";

const repoRoot = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "../..");
const defaultCli = path.join(repoRoot, "build", "windows-dev", process.platform === "win32" ? "sloppy.exe" : "sloppy");
const cliPath = process.env.SLOPPY_TESTHOST_CLI ?? defaultCli;

function bytes(value) {
    return value instanceof Uint8Array ? value : new Uint8Array(value);
}

function concat(chunks) {
    return Buffer.concat(chunks.map((chunk) => Buffer.from(chunk)));
}

function runNative(command, args, options = {}) {
    return new Promise((resolve) => {
        const child = spawn(command, args, {
            cwd: options.cwd,
            env: { ...process.env, ...(options.env ?? {}) },
            stdio: ["ignore", "pipe", "pipe"],
            windowsHide: true,
        });
        const stdout = [];
        const stderr = [];
        let timedOut = false;
        const timer = options.timeoutMs === undefined
            ? undefined
            : setTimeout(() => {
                timedOut = true;
                child.kill("SIGKILL");
            }, options.timeoutMs);
        child.stdout.on("data", (chunk) => stdout.push(chunk));
        child.stderr.on("data", (chunk) => stderr.push(chunk));
        child.on("error", (error) => {
            clearTimeout(timer);
            stderr.push(Buffer.from(String(error.message)));
            resolve({ exitCode: 1, stdout: concat(stdout), stderr: concat(stderr), timedOut });
        });
        child.on("close", (exitCode) => {
            clearTimeout(timer);
            resolve({ exitCode: exitCode ?? 1, stdout: concat(stdout), stderr: concat(stderr), timedOut });
        });
    });
}

function processKey(handle) {
    return `${handle.slot}:${handle.generation}`;
}

function createProcessBridge() {
    let nextSlot = 1;
    const processes = new Map();

    function state(handle) {
        const current = processes.get(processKey(handle));
        if (current === undefined) {
            throw new Error("unknown process handle");
        }
        return current;
    }

    function takeOutput(chunks, maxBytes) {
        if (chunks.length === 0) {
            return new Uint8Array(0);
        }
        const combined = concat(chunks);
        chunks.length = 0;
        const selected = combined.subarray(0, maxBytes);
        if (combined.byteLength > maxBytes) {
            chunks.push(combined.subarray(maxBytes));
        }
        return bytes(selected);
    }

    return {
        processInfo() {
            return {
                pid: process.pid,
                parentPid: process.ppid,
                executablePath: cliPath,
                cwd: process.cwd(),
                args: process.argv,
                argsAvailable: true,
            };
        },
        async processRun(command, args, options) {
            const result = await runNative(command, args, options);
            return {
                command,
                args,
                options,
                exitCode: result.exitCode,
                stdout: bytes(result.stdout),
                stderr: bytes(result.stderr),
                timedOut: result.timedOut,
            };
        },
        processStart(command, args, options) {
            const slot = nextSlot++;
            const handle = Object.freeze({ slot, generation: 1 });
            const child = spawn(command, args, {
                cwd: options.cwd,
                env: { ...process.env, ...(options.env ?? {}) },
                stdio: ["ignore", options.stdout === "pipe" ? "pipe" : "ignore", options.stderr === "pipe" ? "pipe" : "ignore"],
                windowsHide: true,
            });
            const current = {
                child,
                stdout: [],
                stderr: [],
                exit: undefined,
                waiters: [],
            };
            child.stdout?.on("data", (chunk) => current.stdout.push(chunk));
            child.stderr?.on("data", (chunk) => current.stderr.push(chunk));
            child.on("close", (exitCode) => {
                current.exit = { exitCode: exitCode ?? 1, timedOut: false };
                for (const waiter of current.waiters.splice(0)) {
                    waiter(current.exit);
                }
            });
            child.on("error", (error) => {
                current.stderr.push(Buffer.from(String(error.message)));
                current.exit = { exitCode: 1, timedOut: false };
                for (const waiter of current.waiters.splice(0)) {
                    waiter(current.exit);
                }
            });
            processes.set(processKey(handle), current);
            return handle;
        },
        processWait(handle, waitOptions) {
            const current = state(handle);
            if (current.exit !== undefined) {
                return current.exit;
            }
            if ((waitOptions?.timeoutMs ?? 0) === 0) {
                return { exitCode: 0, timedOut: true };
            }
            return new Promise((resolve) => {
                const timer = setTimeout(() => {
                    const index = current.waiters.indexOf(resolve);
                    if (index >= 0) {
                        current.waiters.splice(index, 1);
                    }
                    resolve({ exitCode: 0, timedOut: true });
                }, waitOptions.timeoutMs);
                current.waiters.push((result) => {
                    clearTimeout(timer);
                    resolve(result);
                });
            });
        },
        processTerminate(handle) {
            const current = state(handle);
            if (current.exit === undefined) {
                current.child.kill("SIGINT");
            }
        },
        processKill(handle) {
            const current = state(handle);
            if (current.exit === undefined) {
                current.child.kill("SIGKILL");
            }
        },
        processCancel(handle) {
            return this.processTerminate(handle);
        },
        processReadStdout(handle, maxBytes) {
            return takeOutput(state(handle).stdout, maxBytes);
        },
        processReadStderr(handle, maxBytes) {
            return takeOutput(state(handle).stderr, maxBytes);
        },
        processDispose(handle) {
            processes.delete(processKey(handle));
        },
    };
}

function createConnectionHandle(socket) {
    const handle = {
        socket,
        chunks: [],
        readers: [],
        ended: false,
    };
    socket.on("data", (chunk) => {
        handle.chunks.push(chunk);
        for (const reader of handle.readers.splice(0)) {
            reader();
        }
    });
    socket.on("end", () => {
        handle.ended = true;
        for (const reader of handle.readers.splice(0)) {
            reader();
        }
    });
    socket.on("close", () => {
        handle.ended = true;
        for (const reader of handle.readers.splice(0)) {
            reader();
        }
    });
    socket.on("error", () => {
        handle.ended = true;
        for (const reader of handle.readers.splice(0)) {
            reader();
        }
    });
    return handle;
}

function consume(handle, maxBytes) {
    if (handle.chunks.length === 0) {
        return new Uint8Array(0);
    }
    const combined = concat(handle.chunks);
    handle.chunks.length = 0;
    const selected = combined.subarray(0, maxBytes);
    if (combined.byteLength > maxBytes) {
        handle.chunks.push(combined.subarray(maxBytes));
    }
    return bytes(selected);
}

function createNetBridge() {
    return {
        listen(options) {
            return new Promise((resolve, reject) => {
                const pending = [];
                const acceptors = [];
                const server = net.createServer((socket) => {
                    const handle = createConnectionHandle(socket);
                    const acceptor = acceptors.shift();
                    if (acceptor === undefined) {
                        pending.push(handle);
                    } else {
                        acceptor.resolve(handle);
                    }
                });
                server.once("error", reject);
                server.listen(options.port, options.host, options.backlog ?? 128, () => {
                    server.off("error", reject);
                    resolve({ server, pending, acceptors, closed: false });
                });
            });
        },
        closeListener(handle) {
            handle.closed = true;
            for (const acceptor of handle.acceptors.splice(0)) {
                acceptor.reject(new Error("listener closed"));
            }
            return new Promise((resolve, reject) => {
                handle.server.close((error) => error == null ? resolve() : reject(error));
            });
        },
        abortListener(handle) {
            handle.closed = true;
            for (const acceptor of handle.acceptors.splice(0)) {
                acceptor.reject(new Error("listener closed"));
            }
            handle.server.close();
        },
        accept(handle, timeoutMs) {
            if (handle.pending.length !== 0) {
                return handle.pending.shift();
            }
            if (handle.closed) {
                throw new Error("listener closed");
            }
            return new Promise((resolve, reject) => {
                const acceptor = { resolve, reject };
                let timer;
                if (timeoutMs !== undefined) {
                    timer = setTimeout(() => {
                        const index = handle.acceptors.indexOf(acceptor);
                        if (index >= 0) {
                            handle.acceptors.splice(index, 1);
                        }
                        reject(new Error("accept timed out"));
                    }, timeoutMs);
                }
                acceptor.resolve = (value) => {
                    clearTimeout(timer);
                    resolve(value);
                };
                acceptor.reject = (error) => {
                    clearTimeout(timer);
                    reject(error);
                };
                handle.acceptors.push(acceptor);
            });
        },
        connect(options) {
            return new Promise((resolve, reject) => {
                const socket = net.createConnection({ host: options.host, port: options.port });
                socket.once("connect", () => resolve(createConnectionHandle(socket)));
                socket.once("error", reject);
            });
        },
        write(handle, value) {
            return new Promise((resolve, reject) => {
                handle.socket.write(Buffer.from(value), (error) => error == null ? resolve() : reject(error));
            });
        },
        read(handle, maxBytes) {
            if (handle.chunks.length > 0 || handle.ended) {
                return consume(handle, maxBytes);
            }
            return new Promise((resolve) => {
                handle.readers.push(() => resolve(consume(handle, maxBytes)));
            });
        },
        close(handle) {
            handle.socket.end();
        },
        abort(handle) {
            handle.socket.destroy();
        },
    };
}

function createFsBridge() {
    return {
        async readText(filePath) {
            return fs.readFile(filePath, "utf8");
        },
        async readBytes(filePath) {
            return bytes(await fs.readFile(filePath));
        },
        async directoryCreate(directory, recursive) {
            await fs.mkdir(directory, { recursive: recursive === true });
        },
        async directoryDelete(directory, recursive) {
            await fs.rm(directory, { recursive: recursive === true, force: true });
        },
        async tempDirectory(directory, prefix) {
            await fs.mkdir(directory, { recursive: true });
            return fs.mkdtemp(path.join(directory, prefix));
        },
        async writeBytes(filePath, value) {
            await fs.writeFile(filePath, Buffer.from(value));
        },
    };
}

function installBridge() {
    const previous = globalThis.__sloppy;
    globalThis.__sloppy = {
        ...previous,
        crypto: {
            ...previous?.crypto,
            randomBytes(length) {
                return bytes(crypto.randomBytes(length));
            },
        },
        fs: {
            ...previous?.fs,
            ...createFsBridge(),
        },
        net: {
            ...previous?.net,
            ...createNetBridge(),
        },
        os: {
            ...previous?.os,
            ...createProcessBridge(),
            systemInfo() {
                return {
                    platform: process.platform,
                    arch: process.arch,
                    cpuCount: os.cpus().length,
                    tempDirectory: os.tmpdir(),
                    hostname: os.hostname(),
                    endOfLine: os.EOL,
                };
            },
        },
    };
    return () => {
        if (previous === undefined) {
            delete globalThis.__sloppy;
        } else {
            globalThis.__sloppy = previous;
        }
    };
}

async function occupyPort() {
    const server = net.createServer((socket) => {
        socket.end("HTTP/1.1 200 OK\r\ncontent-length: 2\r\n\r\nok");
    });
    await new Promise((resolve, reject) => {
        server.once("error", reject);
        server.listen(0, "127.0.0.1", resolve);
    });
    return {
        port: server.address().port,
        close() {
            return new Promise((resolve) => server.close(resolve));
        },
    };
}

async function writeFixture(root) {
    const source = path.join(root, "app.js");
    await fs.writeFile(source, `import { Config, Http, Results, Sloppy } from "sloppy";
const Billing = Http.client("billing", {
  baseUrl: Config.required("Billing:BaseUrl"),
});
const app = Sloppy.create();
app.get("/hello", () => Results.text("hello"));
app.post("/echo", (ctx) => Results.ok(ctx.request.json()));
app.get("/billing/{id}", async (ctx) => Results.json(await (await Billing.get("/invoices/{id}", {
  params: { id: ctx.route.id },
  signal: ctx.signal,
}).send()).json()));
export default app;
`, "utf8");
    return source;
}

async function buildFixture(source, root) {
    const artifacts = path.join(root, "artifacts");
    const packageOut = path.join(root, "pkg");
    let result = await runNative(cliPath, ["build", source, "--out", artifacts], { timeoutMs: 30000 });
    assert.equal(result.exitCode, 0, Text.utf8.decode(bytes(result.stderr)));
    result = await runNative(cliPath, ["package", source, "--out", packageOut, "--format", "text"], { timeoutMs: 30000 });
    assert.equal(result.exitCode, 0, Text.utf8.decode(bytes(result.stderr)));
    return { artifacts, packageDir: path.join(packageOut, "package") };
}

async function cliSupportsExecution(artifacts) {
    const result = await runNative(cliPath, ["run", "--artifacts", artifacts, "--once", "GET", "/hello"], { timeoutMs: 30000 });
    if (result.exitCode === 0) {
        return true;
    }
    const stderr = Text.utf8.decode(bytes(result.stderr));
    if (stderr.includes("requires V8-enabled build")) {
        return false;
    }
    assert.fail(stderr);
}

async function assertHello(host) {
    try {
        const response = await host.get("/hello").expectStatus(200);
        assert.equal(await response.text(), "hello");
        await host.openapi.expectRoute("GET", "/hello");
    } finally {
        await host.close();
    }
}

async function assertBillingMock(host, mock, id = "inv_1") {
    try {
        const response = await host.get(`/billing/${id}`).expectStatus(200);
        assert.deepEqual(await response.json(), { id, status: "paid", amount: 42 });
        mock.expectCalled("GET", `/invoices/${id}`).expectNoUnexpectedCalls();
    } finally {
        await host.close();
    }
}

async function assertRequestAfterCloseFails(host) {
    await host.close();
    await host.close();
    await host.dispose();
    await assert.rejects(() => host.get("/hello").send(), /closed/);
}

const restoreBridge = installBridge();
let root;
try {
    await fs.access(cliPath);
    root = await fs.mkdtemp(path.join(os.tmpdir(), "sloppy-testhost-process-"));
    const source = await writeFixture(root);
    const { artifacts, packageDir } = await buildFixture(source, root);

    const occupied = await occupyPort();
    try {
        await assert.rejects(
            () => TestHost.fromArtifacts(artifacts, { mode: "loopback", cliPath, port: occupied.port }),
            /listen|address|port|in use|denied|EADDRINUSE/i,
        );
    } finally {
        await occupied.close();
    }

    const failureHost = await TestHost.fromArtifacts(path.join(root, "missing-artifacts"), { cliPath });
    await assert.rejects(() => failureHost.get("/hello").send(), /Sloppy TestHost request failed/);
    const failureDiagnostic = failureHost.diagnostics.latest();
    assert.equal(failureDiagnostic.code, "SLOPPY_E_TESTHOST_PROCESS_REQUEST");
    assert.match(failureDiagnostic.fields.stderr, /sloppy run:/);
    await failureHost.close();

    const tempBodyRoot = path.join(root, "request-temp");
    const bodyFailureHost = await TestHost.fromArtifacts(path.join(root, "missing-body-artifacts"), {
        cliPath,
        tempDirectory: tempBodyRoot,
    });
    await assert.rejects(() => bodyFailureHost.post("/echo").json({ name: "Ada" }).send(), /Sloppy TestHost request failed/);
    assert.deepEqual(await fs.readdir(tempBodyRoot), []);
    await bodyFailureHost.close();

    const supportsExecution = await cliSupportsExecution(artifacts);
    if (!supportsExecution) {
        console.log("NOT RUN: TestHost artifact/package loopback roundtrip requires a V8-enabled sloppy CLI.");
    } else {
        await assertHello(await TestHost.fromArtifacts(artifacts, { cliPath }));
        await assertHello(await TestHost.fromPackage(packageDir, { cliPath }));
        await assertHello(await TestHost.fromArtifacts(artifacts, { mode: "loopback", cliPath }));
        await assertHello(await TestHost.fromPackage(packageDir, { mode: "loopback", cliPath }));

        const artifactMock = TestHttp.mock()
            .get("/invoices/inv_1")
            .replyJson(200, { id: "inv_1", status: "paid", amount: 42 });
        await assertBillingMock(await TestHost.fromArtifacts(artifacts, {
            cliPath,
            httpClients: { billing: artifactMock },
        }), artifactMock);

        const packageMock = TestHttp.mock()
            .get("/invoices/inv_1")
            .replyJson(200, { id: "inv_1", status: "paid", amount: 42 });
        await assertBillingMock(await TestHost.fromPackage(packageDir, {
            cliPath,
            httpClients: { billing: packageMock },
        }), packageMock);

        const loopbackMock = TestHttp.mock()
            .get("/invoices/inv_1")
            .replyJson(200, { id: "inv_1", status: "paid", amount: 42 });
        await assertBillingMock(await TestHost.fromArtifacts(artifacts, {
            mode: "loopback",
            cliPath,
            httpClients: { billing: loopbackMock },
        }), loopbackMock);

        const packageLoopbackMock = TestHttp.mock()
            .get("/invoices/inv_1")
            .replyJson(200, { id: "inv_1", status: "paid", amount: 42 });
        await assertBillingMock(await TestHost.fromPackage(packageDir, {
            mode: "loopback",
            cliPath,
            httpClients: { billing: packageLoopbackMock },
        }), packageLoopbackMock);

        const unexpectedMock = TestHttp.mock()
            .get("/invoices/other")
            .replyJson(200, { id: "other", status: "paid", amount: 42 });
        const unexpectedHost = await TestHost.fromArtifacts(artifacts, {
            cliPath,
            httpClients: { billing: unexpectedMock },
        });
        try {
            await unexpectedHost.get("/billing/inv_1").expectStatus(500);
            assert.throws(() => unexpectedMock.expectNoUnexpectedCalls(), /Unexpected outbound HTTP calls/);
        } finally {
            await unexpectedHost.close();
        }

        const [first, second] = await Promise.all([
            TestHost.fromArtifacts(artifacts, { mode: "loopback", cliPath }),
            TestHost.fromArtifacts(artifacts, { mode: "loopback", cliPath }),
        ]);
        try {
            assert.notEqual(first.port, second.port);
            assert.equal(await (await first.get("/hello")).text(), "hello");
            assert.equal(await (await second.get("/hello")).text(), "hello");
        } finally {
            await Promise.all([first.close(), second.close()]);
        }

        const closeHost = await TestHost.fromArtifacts(artifacts, { mode: "loopback", cliPath });
        await assertRequestAfterCloseFails(closeHost);

        const isolatedA = await TestHost.fromArtifacts(artifacts, { cliPath });
        const isolatedB = await TestHost.fromArtifacts(artifacts, { cliPath });
        try {
            isolatedA.diagnostics.record({ code: "SLOPPY_TESTHOST_ISOLATED" });
            assert.equal(isolatedA.diagnostics.filter({ code: "SLOPPY_TESTHOST_ISOLATED" }).length, 1);
            assert.equal(isolatedB.diagnostics.filter({ code: "SLOPPY_TESTHOST_ISOLATED" }).length, 0);
        } finally {
            await isolatedA.close();
            await isolatedB.close();
        }
    }
} finally {
    if (root !== undefined) {
        await fs.rm(root, { recursive: true, force: true });
    }
    restoreBridge();
}
