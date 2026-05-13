import assert from "node:assert/strict";

import { Redis, Text } from "../../stdlib/sloppy/index.js";

function decodeCommand(bytes) {
    const lines = Text.utf8.decode(bytes).split("\r\n");
    const args = [];
    for (let index = 1; index < lines.length - 1;) {
        args.push(lines[index + 1]);
        index += 2;
    }
    return args;
}

function bulk(value) {
    return value === undefined ? "$-1\r\n" : `$${value.length}\r\n${value}\r\n`;
}

function bridgeWithLocks() {
    let nextHandle = 1;
    const handles = new Map();
    const values = new Map();
    let loadedScript = "";
    function reply(command) {
        switch (command[0]) {
            case "PING":
                return "+PONG\r\n";
            case "SET": {
                const key = command[1];
                const owner = command[2];
                const nx = command.includes("NX");
                if (nx && values.has(key)) {
                    return "$-1\r\n";
                }
                values.set(key, owner);
                return "+OK\r\n";
            }
            case "GET":
                return bulk(values.get(command[1]));
            case "DEL":
                return `:${values.delete(command[1]) ? 1 : 0}\r\n`;
            case "SCRIPT":
                loadedScript = command[2];
                return bulk("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");
            case "EVALSHA": {
                const key = command[3];
                const owner = command[4];
                if (values.get(key) !== owner) {
                    return ":0\r\n";
                }
                if (loadedScript.includes("PEXPIRE")) {
                    return ":1\r\n";
                }
                values.delete(key);
                return ":1\r\n";
            }
            default:
                return "+OK\r\n";
        }
    }
    return {
        connect() {
            handles.set(nextHandle, { reads: [] });
            return Promise.resolve(nextHandle++);
        },
        async write(handle, bytes) {
            handles.get(handle).reads.push(Text.utf8.encode(reply(decodeCommand(bytes))));
        },
        async read(handle) {
            return handles.get(handle).reads.shift() ?? new Uint8Array(0);
        },
        async close() {},
        async abort() {},
        values,
    };
}

function installBridge(bridge) {
    const previous = globalThis.__sloppy;
    globalThis.__sloppy = { ...(previous ?? {}), net: bridge };
    return () => {
        globalThis.__sloppy = previous;
    };
}

const bridge = bridgeWithLocks();
const restore = installBridge(bridge);
try {
    const redis = Redis.client("locks", {
        url: "redis://localhost/0",
        commandTimeoutMs: 100,
        pool: { maxConnections: 1, pendingQueueLimit: 4 },
    });
    const locks = Redis.locks(redis, { prefix: "tests:locks:" });
    const lease = await locks.acquire("jobs:leader", { ttlMs: 1000 });
    assert.equal(bridge.values.has("tests:locks:jobs:leader"), true);
    await assert.rejects(() => lease.extend(), /SLOPPY_E_REDIS_INVALID_OPTIONS/u);
    await assert.rejects(() => lease.extend(0), /SLOPPY_E_REDIS_INVALID_OPTIONS/u);
    assert.equal(await lease.extend(2000), true);
    await assert.rejects(
        () => locks.acquire("jobs:leader", { ttlMs: 1000, waitTimeoutMs: 1, retryDelayMs: 1 }),
        /SLOPPY_E_REDIS_LOCK_TIMEOUT/u,
    );
    bridge.values.set("tests:locks:jobs:leader", "other-owner");
    await assert.rejects(() => lease.extend(2000), /SLOPPY_E_REDIS_LOCK_LOST/u);
    await assert.rejects(() => lease.release(), /SLOPPY_E_REDIS_LOCK_RELEASE_FAILED/u);
    await lease.dispose();
    bridge.values.delete("tests:locks:jobs:leader");
    const second = await locks.acquire("jobs:leader", { ttlMs: 1000 });
    assert.equal(await second.release(), true);
    await second.dispose();
    await redis.dispose();
} finally {
    restore();
}
