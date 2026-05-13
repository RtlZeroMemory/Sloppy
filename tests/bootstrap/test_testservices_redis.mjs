import assert from "node:assert/strict";

import { TestServices, Text } from "../../stdlib/sloppy/index.js";

function decodeCommand(bytes) {
    const lines = Text.utf8.decode(bytes).split("\r\n");
    const args = [];
    for (let index = 1; index < lines.length - 1;) {
        args.push(lines[index + 1]);
        index += 2;
    }
    return args;
}

function redisBridge() {
    let nextHandle = 1;
    const handles = new Map();
    return {
        connect() {
            handles.set(nextHandle, { reads: [] });
            return Promise.resolve(nextHandle++);
        },
        async write(handle, bytes) {
            const command = decodeCommand(bytes);
            handles.get(handle).reads.push(Text.utf8.encode(command[0] === "PING" ? "+PONG\r\n" : "+OK\r\n"));
        },
        async read(handle) {
            return handles.get(handle).reads.shift() ?? new Uint8Array(0);
        },
        async close() {},
        async abort() {},
    };
}

function failingRedisBridge() {
    return {
        async connect() {
            throw new Error("redis://:secret@127.0.0.1:49199/0 failed");
        },
        async write() {},
        async read() {
            return new Uint8Array(0);
        },
        async close() {},
        async abort() {},
    };
}

function installBridge(bridge) {
    const previous = globalThis.__sloppy;
    globalThis.__sloppy = { ...(previous ?? {}), net: bridge };
    return () => {
        globalThis.__sloppy = previous;
    };
}

class FakeDockerBackend {
    constructor(expectPassword = true) {
        this.commands = [];
        this.expectPassword = expectPassword;
    }

    async run(args) {
        this.commands.push(args);
        if (args[0] === "version") {
            return { exitCode: 0, stdout: "{}", stderr: "", timedOut: false };
        }
        if (args[0] === "image" && args[1] === "inspect") {
            return { exitCode: 0, stdout: "[]", stderr: "", timedOut: false };
        }
        if (args[0] === "create") {
            assert(args.includes("redis:7-alpine"));
            if (this.expectPassword) {
                assert(args.includes("--requirepass"));
                assert(args.includes("secret"));
            } else {
                assert.equal(args.includes("--requirepass"), false);
            }
            return { exitCode: 0, stdout: "redis-container\n", stderr: "", timedOut: false };
        }
        if (args[0] === "start") {
            return { exitCode: 0, stdout: "", stderr: "", timedOut: false };
        }
        if (args[0] === "inspect") {
            return {
                exitCode: 0,
                stdout: JSON.stringify([{
                    NetworkSettings: {
                        Ports: {
                            "6379/tcp": [{ HostPort: "49199" }],
                        },
                    },
                }]),
                stderr: "",
                timedOut: false,
            };
        }
        if (args[0] === "logs") {
            return { exitCode: 0, stdout: "Ready to accept connections\n", stderr: "", timedOut: false };
        }
        if (args[0] === "stop" || args[0] === "rm") {
            return { exitCode: 0, stdout: "", stderr: "", timedOut: false };
        }
        return { exitCode: 0, stdout: "", stderr: "", timedOut: false };
    }
}

class FailingCleanupDockerBackend extends FakeDockerBackend {
    async run(args) {
        if (args[0] === "stop" || args[0] === "rm") {
            this.commands.push(args);
            return { exitCode: 1, stdout: "", stderr: "cleanup failed for secret", timedOut: false };
        }
        return super.run(args);
    }
}

const dockerBackend = new FakeDockerBackend();
const restore = installBridge(redisBridge());
let redis;
try {
    redis = await TestServices.redis({
        password: "secret",
        dockerBackend,
        startupTimeoutMs: 100,
        readinessTimeoutMs: 100,
    });
    assert.equal(redis.url, "redis://:secret@127.0.0.1:49199/0");
    assert.equal(redis.env()["Redis:Url"], redis.url);
    assert.equal(redis.env().REDIS_URL, redis.url);
    assert.equal(redis.env().REDIS_PASSWORD, "secret");
    assert.equal(await redis.client("from-service").ping(), "PONG");
    assert.equal(JSON.stringify(redis.diagnostics()).includes("secret"), false);
    await redis.flush();
    await redis.reset();
} finally {
    await redis?.dispose();
    restore();
}

assert(dockerBackend.commands.some((args) => args[0] === "rm"));

{
    const noPasswordBackend = new FakeDockerBackend(false);
    const noPasswordRestore = installBridge(redisBridge());
    let noPasswordRedis;
    try {
        noPasswordRedis = await TestServices.redis({
            dockerBackend: noPasswordBackend,
            startupTimeoutMs: 100,
            readinessTimeoutMs: 100,
        });
        assert.equal(noPasswordRedis.url, "redis://127.0.0.1:49199/0");
        assert.equal(Object.hasOwn(noPasswordRedis.env(), "REDIS_PASSWORD"), false);
    } finally {
        await noPasswordRedis?.dispose();
        noPasswordRestore();
    }
}

{
    const failureBackend = new FailingCleanupDockerBackend();
    const failureRestore = installBridge(failingRedisBridge());
    try {
        await assert.rejects(
            () => TestServices.redis({
                password: "secret",
                dockerBackend: failureBackend,
                startupTimeoutMs: 100,
                readinessTimeoutMs: 1,
            }),
            (error) => {
                const message = String(error?.message ?? error);
                assert.match(message, /Mapped port:\s+49199/s);
                assert.match(message, /Cleanup failures:/);
                assert.equal(message.includes("secret"), false);
                return true;
            },
        );
    } finally {
        failureRestore();
    }
}

for (const [name, start] of [
    ["postgres", TestServices.postgres],
    ["sqlServer", TestServices.sqlServer],
]) {
    const unavailableBackend = {
        commands: [],
        async run(args) {
            this.commands.push(args);
            return { exitCode: 1, stdout: "", stderr: "docker should not be touched", timedOut: false };
        },
    };
    await assert.rejects(
        () => start({ dockerBackend: unavailableBackend, startupTimeoutMs: 1, readinessTimeoutMs: 1 }),
        (error) => {
            assert.equal(error?.code, "SLOPPY_E_TESTSERVICES_PROVIDER_UNAVAILABLE", `${name} should check provider before Docker`);
            return true;
        },
    );
    assert.deepEqual(unavailableBackend.commands, []);
}
