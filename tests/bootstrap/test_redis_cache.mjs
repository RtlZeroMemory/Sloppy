import assert from "node:assert/strict";

import { Cache, Redis, Schema, Text } from "../../stdlib/sloppy/index.js";

function decodeCommands(bytes) {
    const text = Text.utf8.decode(bytes);
    const lines = text.split("\r\n");
    const commands = [];
    for (let index = 0; index < lines.length - 1;) {
        const count = Number(lines[index].slice(1));
        const args = [];
        index += 1;
        for (let arg = 0; arg < count; arg += 1) {
            args.push(lines[index + 1]);
            index += 2;
        }
        commands.push(args);
    }
    return commands;
}

function bulk(value) {
    if (value === null || value === undefined) {
        return "$-1\r\n";
    }
    return `$${Text.utf8.encode(value).byteLength}\r\n${value}\r\n`;
}

function array(values) {
    return `*${values.length}\r\n${values.join("")}`;
}

function bridgeWithStore() {
    let nextHandle = 1;
    const handles = new Map();
    const values = new Map();
    const sets = new Map();
    const scripts = new Map();
    function sadd(key, value) {
        if (!sets.has(key)) {
            sets.set(key, new Set());
        }
        sets.get(key).add(value);
    }
    function reply(command) {
        switch (command[0]) {
            case "PING":
                return "+PONG\r\n";
            case "SET":
                values.set(command[1], command[2]);
                return "+OK\r\n";
            case "GET":
                return bulk(values.get(command[1]));
            case "DEL": {
                let count = 0;
                for (const key of command.slice(1)) {
                    if (values.delete(key) || sets.delete(key)) {
                        count += 1;
                    }
                }
                return `:${count}\r\n`;
            }
            case "EXISTS":
                return `:${values.has(command[1]) ? 1 : 0}\r\n`;
            case "SADD":
                for (const value of command.slice(2)) {
                    sadd(command[1], value);
                }
                return `:${command.length - 2}\r\n`;
            case "SREM": {
                const set = sets.get(command[1]);
                let removed = 0;
                for (const value of command.slice(2)) {
                    if (set?.delete(value)) {
                        removed += 1;
                    }
                }
                return `:${removed}\r\n`;
            }
            case "PEXPIRE":
                return ":1\r\n";
            case "SCRIPT": {
                const sha = String(scripts.size + 1).padStart(40, "0");
                scripts.set(sha, command[2]);
                return bulk(sha);
            }
            case "EVALSHA": {
                const loadedScript = scripts.get(command[1]) ?? "";
                const numKeys = Number(command[2]);
                const keys = command.slice(3, 3 + numKeys);
                const args = command.slice(3 + numKeys);
                if (loadedScript.includes("reverseKey = key .. ARGV[1]")) {
                    const entries = [...(sets.get(keys[0]) ?? new Set())];
                    for (const key of entries) {
                        const reverseKey = `${key}${args[0]}`;
                        for (const tagKey of sets.get(reverseKey) ?? []) {
                            sets.get(tagKey)?.delete(key);
                        }
                        sets.delete(reverseKey);
                        values.delete(key);
                        sets.get(keys[1])?.delete(key);
                        sets.get(keys[1])?.delete(reverseKey);
                    }
                    sets.delete(keys[0]);
                    sets.get(keys[1])?.delete(keys[0]);
                    return `:${entries.length}\r\n`;
                }
                if (loadedScript.includes("return redis.call(\"DEL\", KEYS[1])")) {
                    for (const tagKey of sets.get(keys[2]) ?? []) {
                        sets.get(tagKey)?.delete(keys[0]);
                    }
                    sets.delete(keys[2]);
                    sets.get(keys[1])?.delete(keys[0]);
                    sets.get(keys[1])?.delete(keys[2]);
                    return `:${values.delete(keys[0]) ? 1 : 0}\r\n`;
                }
                if (loadedScript.includes("redis.call(\"PEXPIRE\", KEYS[1], ARGV[1])")) {
                    return `:${values.has(keys[0]) ? 1 : 0}\r\n`;
                }
                for (const tagKey of sets.get(keys[2]) ?? []) {
                    sets.get(tagKey)?.delete(keys[0]);
                }
                sets.delete(keys[2]);
                values.set(keys[0], args[0]);
                sadd(keys[1], keys[0]);
                if (keys.length > 3) {
                    sadd(keys[1], keys[2]);
                }
                for (const tagKey of keys.slice(3)) {
                    sadd(tagKey, keys[0]);
                    sadd(keys[2], tagKey);
                    sadd(keys[1], tagKey);
                }
                return ":1\r\n";
            }
            case "SCAN": {
                const match = command[2] === "MATCH" ? command[3].replace("*", "") : "";
                const keys = [...values.keys(), ...sets.keys()].filter((key) => key.startsWith(match));
                return array([bulk("0"), array(keys.map(bulk))]);
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
            for (const command of decodeCommands(bytes)) {
                handles.get(handle).reads.push(Text.utf8.encode(reply(command)));
            }
        },
        async read(handle) {
            return handles.get(handle).reads.shift() ?? new Uint8Array(0);
        },
        async close() {},
        async abort() {},
        values,
        sets,
    };
}

function installBridge(bridge) {
    const previous = globalThis.__sloppy;
    globalThis.__sloppy = { ...(previous ?? {}), net: bridge };
    return () => {
        globalThis.__sloppy = previous;
    };
}

const bridge = bridgeWithStore();
const restore = installBridge(bridge);
try {
    const redis = Redis.client("cache", {
        url: "redis://localhost/0",
        commandTimeoutMs: 100,
        pool: { maxConnections: 1, pendingQueueLimit: 4 },
    });
    const cache = Cache.redis("default", {
        client: redis,
        prefix: "tests:cache:",
        ttlMs: 1000,
        maxValueBytes: 1024,
    });

    assert.equal(await cache.set("hello", { ok: true }, { ttlMs: 1000 }), true);
    assert.deepEqual(await cache.get("hello", { schema: Schema.object({ ok: Schema.boolean() }) }), { ok: true });
    assert.equal(await cache.has("hello"), true);
    assert.equal(await cache.remove("hello"), true);
    assert.equal(await cache.get("hello"), undefined);

    await cache.set("plain-string", "value");
    await cache.set("plain-object", { value: true });
    await cache.set("plain-null", null);
    await cache.set("tagged-object", { tagged: true }, { tags: ["format"] });
    assert.deepEqual(await cache.get("plain-string"), "value");
    assert.deepEqual(await cache.get("plain-object"), { value: true });
    assert.equal(await cache.get("plain-null"), null);
    assert.deepEqual(await cache.get("tagged-object"), { tagged: true });
    assert.equal([...bridge.values.values()].every((value) => value.startsWith("J:")), true);
    await assert.rejects(
        () => Cache.redis("small", { client: redis, prefix: "tests:small:", maxValueBytes: 2 }).set("too-large", { value: true }),
        /SLOPPY_E_REDIS_VALUE_TOO_LARGE/u,
    );

    assert.deepEqual(await cache.getOrCreate("coalesced", { ttlMs: 1000 }, async () => ({ value: 1 })), { value: 1 });
    assert.deepEqual(await cache.getOrCreate("coalesced", { ttlMs: 1000 }, async () => ({ value: 2 })), { value: 1 });
    assert.deepEqual(await cache.getOrCreate("natural", async () => ({ value: 3 }), { ttlMs: 1000 }), { value: 3 });

    await cache.set("tagged", { tagged: true }, { tags: ["user:1"] });
    assert.deepEqual(await cache.get("tagged"), { tagged: true });
    assert.equal(await cache.invalidateTag("user:1"), 1);
    assert.equal(await cache.get("tagged"), undefined);

    await cache.set("multi", { tagged: true }, { tags: ["a", "b"] });
    assert.equal(await cache.invalidateTag("a"), 1);
    assert.equal(await cache.invalidateTag("b"), 0);

    await cache.set("removed", { tagged: true }, { tags: ["remove-a", "remove-b"] });
    assert.equal(await cache.remove("removed"), true);
    assert.equal(await cache.invalidateTag("remove-a"), 0);
    assert.equal(await cache.invalidateTag("remove-b"), 0);

    await cache.set("a", 1);
    await cache.set("b", 2);
    assert.equal((await cache.clear()) >= 3, true);
    assert.equal(cache.stats().sets >= 5, true);
    assert.equal((await cache.health()).status, "healthy");

    const ownedCache = Cache.redis("owned", {
        url: "redis://localhost/0",
        prefix: "tests:owned:",
        ttlMs: 1000,
    });
    assert.equal(await ownedCache.set("hello", { owned: true }), true);
    assert.deepEqual(await ownedCache.get("hello"), { owned: true });
    await ownedCache.dispose();

    await cache.dispose();
    await redis.dispose();
} finally {
    restore();
}
