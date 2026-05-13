import assert from "node:assert/strict";

const previousSloppy = globalThis.__sloppy;
globalThis.__sloppy = {
    ...(previousSloppy ?? {}),
    crypto: {
        ...previousSloppy?.crypto,
        randomHex(length) {
            return "ab".repeat(length);
        },
    },
};

await import("../../stdlib/sloppy/internal/runtime-classic.js");

try {
    const { Cache, Redis, Text } = globalThis.__sloppy_runtime;

    {
        const stored = new Map();
        const fakeRedis = Object.freeze({
            name: "classic-cache",
            async command(command, args) {
                if (command === "GET") {
                    const value = stored.get(args[0]);
                    return value === undefined ? null : Text.utf8.encode(value);
                }
                return 1;
            },
            async script(script, keys, args) {
                if (script.includes("redis.call(\"SET\", KEYS[1], ARGV[1]")) {
                    stored.set(keys[0], args[0]);
                    return 1;
                }
                if (script.includes("return redis.call(\"DEL\", KEYS[1])")) {
                    return stored.delete(keys[0]) ? 1 : 0;
                }
                return 1;
            },
            async exists(key) {
                return stored.has(key);
            },
            async scan() {
                return { cursor: "0", keys: [] };
            },
            async pipeline(items) {
                return items.map(() => 1);
            },
            async health() {
                return { status: "healthy" };
            },
        });
        const cache = Cache.redis(fakeRedis, { name: "classic", ttlMs: 1000 });
        await cache.set("nil", null);
        assert.equal(await cache.get("nil"), null);
        assert.equal([...stored.values()].every((value) => String(value).startsWith("J:")), true);
        stored.set(`sloppy:cache:classic:entry:${Cache.keyHash("bad-b64")}`, "B:not base64 !!!");
        await assert.rejects(() => cache.get("bad-b64"), /SLOPPY_E_REDIS_RESPONSE_VALIDATION_FAILED/u);
    }

    {
        const calls = [];
        const fakeRedis = Object.freeze({
            async command(command, args) {
                calls.push([command, args]);
                return "OK";
            },
            async script(script, keys, args) {
                calls.push(["SCRIPT", keys, args]);
                return 1;
            },
        });
        const locks = Redis.locks(fakeRedis, { prefix: "classic:locks:" });
        const lease = await locks.acquire("leader", { ttlMs: 1000 });
        await assert.rejects(() => lease.extend(), /SLOPPY_E_REDIS_INVALID_OPTIONS/u);
        await assert.rejects(() => lease.extend(0), /SLOPPY_E_REDIS_INVALID_OPTIONS/u);
        assert.equal(await lease.extend(2000), true);
        await lease.dispose();
        assert.equal(calls.some((entry) => entry[0] === "SCRIPT" && entry[2]?.[1] === 2000), true);
    }
} finally {
    if (previousSloppy === undefined) {
        delete globalThis.__sloppy;
    } else {
        globalThis.__sloppy = previousSloppy;
    }
}
