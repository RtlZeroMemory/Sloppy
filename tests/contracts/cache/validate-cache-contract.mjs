import assert from "node:assert/strict";

import { ContractAssertionCollector, errorInvariants } from "../runner/assertions.mjs";
import { createFinding, createReport } from "../runner/contract-report.mjs";
import { Cache, Redis, Results, Schema, Sloppy, TestHost, Text } from "../../../stdlib/sloppy/index.js";

const SUBSYSTEM = "cache";
const SECRET_VALUE = "cached-user-secret-value";
const REDIS_SECRET = "redis-contract-password";

async function runInvariant(collector, invariant, callback) {
    try {
        await callback();
        collector.pass(invariant, `${invariant} contract holds`);
    } catch (error) {
        collector.fail(invariant, `${invariant} contract failed`, {
            error: error?.message ?? String(error),
        });
    }
}

async function expectInvariantFailure(invariant, callback) {
    const collector = new ContractAssertionCollector({ subsystem: SUBSYSTEM, fixture: "negative-probe" });
    await runInvariant(collector, invariant, callback);
    const detected = errorInvariants(collector.findings);
    assert.equal(detected.includes(invariant), true);
    return detected;
}

function expectedFailureFinding(invariant, detected) {
    return createFinding({
        id: `${SUBSYSTEM}.negative.${invariant}`,
        status: "pass",
        severity: "info",
        subsystem: SUBSYSTEM,
        invariant: `negative.${invariant}`,
        fixture: "negative-probe",
        message: `broken probe produced expected ${invariant} finding`,
        details: { detected },
    });
}

function user(sub, roles = []) {
    return Object.freeze({
        authenticated: true,
        sub,
        roles: Object.freeze(roles),
        claims: Object.freeze({ sub }),
        hasRole(role) {
            return roles.includes(role);
        },
        hasClaim(claim) {
            return claim === "sub";
        },
        claim(claim) {
            return this.claims[claim];
        },
    });
}

function assertNoSecret(value, secret = SECRET_VALUE) {
    assert.equal(JSON.stringify(value).includes(secret), false);
}

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

function deterministicRedisBridge() {
    let nextHandle = 1;
    let loadedScript = "";
    let forceReleaseFailure = false;
    const handles = new Map();
    const values = new Map();
    const sets = new Map();
    const expirations = new Map();

    function now() {
        return Date.now();
    }

    function isExpired(key) {
        const expiresAt = expirations.get(key);
        if (expiresAt === undefined || expiresAt > now()) {
            return false;
        }
        values.delete(key);
        expirations.delete(key);
        return true;
    }

    function addSet(name, value) {
        const set = sets.get(name) ?? new Set();
        set.add(value);
        sets.set(name, set);
    }

    function removeSet(name, value) {
        sets.get(name)?.delete(value);
    }

    function setMembers(name) {
        return [...(sets.get(name) ?? new Set())];
    }

    function consumeForcedReleaseFailure() {
        const shouldFail = forceReleaseFailure;
        forceReleaseFailure = false;
        return shouldFail;
    }

    function integer(value) {
        return `:${value}\r\n`;
    }

    function array(values) {
        return `*${values.length}\r\n${values.map((value) => bulk(value)).join("")}`;
    }

    function reply(command) {
        switch (command[0]) {
        case "PING":
            return "+PONG\r\n";
        case "AUTH":
        case "SELECT":
            return "+OK\r\n";
        case "GET": {
            isExpired(command[1]);
            return bulk(values.get(command[1]));
        }
        case "SET": {
            const key = command[1];
            const value = command[2];
            if (command.includes("NX") && values.has(key) && !isExpired(key)) {
                return "$-1\r\n";
            }
            values.set(key, value);
            const pxIndex = command.indexOf("PX");
            if (pxIndex >= 0) {
                expirations.set(key, now() + Number(command[pxIndex + 1]));
            }
            return "+OK\r\n";
        }
        case "DEL": {
            let removed = 0;
            for (const key of command.slice(1)) {
                removed += values.delete(key) || sets.delete(key) ? 1 : 0;
                expirations.delete(key);
            }
            return integer(removed);
        }
        case "EXISTS":
            isExpired(command[1]);
            return integer(values.has(command[1]) ? 1 : 0);
        case "SADD":
            for (const value of command.slice(2)) {
                addSet(command[1], value);
            }
            return integer(command.length - 2);
        case "SREM":
            for (const value of command.slice(2)) {
                removeSet(command[1], value);
            }
            return integer(command.length - 2);
        case "SMEMBERS":
            return array(setMembers(command[1]));
        case "PEXPIRE":
            expirations.set(command[1], now() + Number(command[2]));
            return integer(1);
        case "SCAN": {
            const match = command[command.indexOf("MATCH") + 1] ?? "*";
            const prefix = match.endsWith("*") ? match.slice(0, -1) : match;
            return `*2\r\n${bulk("0")}${array([...values.keys(), ...sets.keys()].filter((key) => key.startsWith(prefix)))}`;
        }
        case "SCRIPT":
            loadedScript = command[2];
            return bulk("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");
        case "EVALSHA":
            return scriptReply(command);
        default:
            return "+OK\r\n";
        }
    }

    function scriptReply(command) {
        const keyCount = Number(command[2]);
        const keys = command.slice(3, 3 + keyCount);
        const args = command.slice(3 + keyCount);
        if (loadedScript.includes("SMEMBERS") && loadedScript.includes("SREM") && loadedScript.includes("SET")) {
            values.set(keys[0], args[0]);
            expirations.set(keys[0], now() + Number(args[1]));
            addSet(keys[1], keys[0]);
            if (keys.length > 3) {
                sets.set(keys[2], new Set(keys.slice(3)));
                addSet(keys[1], keys[2]);
            }
            for (const tagKey of keys.slice(3)) {
                addSet(tagKey, keys[0]);
                addSet(keys[2], tagKey);
                addSet(keys[1], tagKey);
            }
            return integer(1);
        }
        if (loadedScript.includes("return redis.call(\"DEL\", KEYS[1])")) {
            const tags = setMembers(keys[2]);
            for (const tagKey of tags) {
                removeSet(tagKey, keys[0]);
            }
            sets.delete(keys[2]);
            removeSet(keys[1], keys[0]);
            removeSet(keys[1], keys[2]);
            const removed = values.delete(keys[0]) ? 1 : 0;
            return integer(removed);
        }
        if (loadedScript.includes("return #entries")) {
            const entries = setMembers(keys[0]);
            for (const key of entries) {
                const reverseKey = `${key}${args[0]}`;
                for (const tagKey of setMembers(reverseKey)) {
                    removeSet(tagKey, key);
                }
                values.delete(key);
                sets.delete(reverseKey);
                removeSet(keys[1], key);
                removeSet(keys[1], reverseKey);
            }
            sets.delete(keys[0]);
            removeSet(keys[1], keys[0]);
            return integer(entries.length);
        }
        if (loadedScript.includes("PEXPIRE")) {
            if (consumeForcedReleaseFailure()) {
                return integer(0);
            }
            if (values.get(keys[0]) !== args[0]) {
                return integer(0);
            }
            expirations.set(keys[0], now() + Number(args[1]));
            return integer(1);
        }
        if (consumeForcedReleaseFailure()) {
            return integer(0);
        }
        if (values.get(keys[0]) !== args[0]) {
            return integer(0);
        }
        values.delete(keys[0]);
        return integer(1);
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
        forceNextReleaseFailure() {
            forceReleaseFailure = true;
        },
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

async function withRedisBridge(callback) {
    const bridge = deterministicRedisBridge();
    const restore = installBridge(bridge);
    try {
        return await callback(bridge);
    } finally {
        restore();
    }
}

async function validateMemoryCache(collector) {
    await runInvariant(collector, "cache.set-get", async () => {
        const cache = Cache.memory("contract-set-get", { maxEntries: 10 });
        await cache.set("profile:1", { id: 1, name: "Ada" });
        assert.deepEqual(await cache.get("profile:1"), { id: 1, name: "Ada" });
        assert.equal(await cache.get("missing"), undefined);
    });

    await runInvariant(collector, "cache.delete", async () => {
        const cache = Cache.memory("contract-delete", { maxEntries: 10 });
        await cache.set("remove-me", { ok: true });
        assert.equal(await cache.delete("remove-me"), true);
        assert.equal(await cache.get("remove-me"), undefined);
        await cache.set("clear-me", { ok: true });
        assert.equal(await cache.clear() >= 1, true);
        assert.equal(await cache.get("clear-me"), undefined);
    });

    await runInvariant(collector, "cache.ttl-expiry", async () => {
        let current = Date.parse("2026-05-14T00:00:00.000Z");
        const clock = {
            now() {
                return new Date(current);
            },
        };
        const cache = Cache.memory("contract-ttl", { maxEntries: 10, clock });
        await cache.set("short", { value: "lived" }, { ttlMs: 50 });
        assert.deepEqual(await cache.get("short"), { value: "lived" });
        current += 51;
        assert.equal(await cache.get("short"), undefined);
    });

    await runInvariant(collector, "cache.tag-invalidation", async () => {
        const cache = Cache.memory("contract-tags", { maxEntries: 10 });
        await cache.set("user:1", { private: true }, { tags: ["users"] });
        assert.equal(await cache.invalidateTag("users"), 1);
        assert.equal(await cache.get("user:1"), undefined);
    });

    await runInvariant(collector, "cache.schema-validation", async () => {
        const cache = Cache.memory("contract-schema", { maxEntries: 10 });
        await cache.set("typed", { id: 1 }, { schema: Schema.object({ id: Schema.integer() }) });
        assert.deepEqual(await cache.get("typed", Schema.object({ id: Schema.integer() })), { id: 1 });
        await assert.rejects(() => cache.get("typed", Schema.object({ id: Schema.string() })), /schema validation/u);
        await assert.rejects(
            () => cache.set("too-large", { value: "abcdef" }, { maxValueBytes: 2 }),
            /SLOPPY_E_CACHE_VALUE_TOO_LARGE|maxValueBytes/u,
        );
        await assert.rejects(() => cache.set("", { ok: true }), /cache key/u);
    });
}

async function validateOutputCache(collector) {
    const app = Sloppy.create();
    const cache = Cache.memory("default", { maxEntries: 100 });
    app.services.addCache(cache);
    let products = 0;
    app.get("/products/{id:int}", (ctx) => {
        products += 1;
        return Results.json({ calls: products, id: ctx.route.id }, {
            status: 203,
            headers: { "X-Contract": "products" },
        });
    }).outputCache({
        ttlMs: 1000,
        varyByRouteParams: ["id"],
        tags: ["products"],
    });
    let queryCalls = 0;
    app.get("/search", (ctx) => {
        queryCalls += 1;
        return Results.json({ calls: queryCalls, query: ctx.query.category ?? "" });
    }).outputCache({ ttlMs: 1000, varyByQuery: ["category"] });
    let headerCalls = 0;
    app.get("/localized", (ctx) => {
        headerCalls += 1;
        return Results.json({ calls: headerCalls, language: ctx.request.headers.get("accept-language") });
    }).outputCache({ ttlMs: 1000, varyByHeader: ["accept-language"] });
    let userCalls = 0;
    app.get("/me", (ctx) => {
        userCalls += 1;
        return Results.json({ calls: userCalls, sub: ctx.user.sub, secret: SECRET_VALUE });
    }).requiresAuth().outputCache({ ttlMs: 1000, varyByUser: true });
    app.get("/unsafe-me", (ctx) => Results.json({ sub: ctx.user.sub, secret: SECRET_VALUE }))
        .requiresAuth()
        .outputCache({ ttlMs: 1000 });
    app.post("/mutate", () => Results.json({ ok: true })).outputCache({ ttlMs: 1000 });
    app.get("/cookie", () => Results.json({ ok: true }).cookie("sid", SECRET_VALUE)).outputCache({ ttlMs: 1000 });
    app.get("/problem", () => Results.problem("broken", { status: 500 })).outputCache({ ttlMs: 1000, statusCodes: [500] });
    const host = await TestHost.create(app);
    try {
        await runInvariant(collector, "output-cache.get-only", async () => {
            assert.equal((await host.get("/products/1")).headers.get("x-sloppy-output-cache"), "MISS");
            assert.equal((await host.get("/products/1")).headers.get("x-sloppy-output-cache"), "HIT");
            assert.equal((await host.post("/mutate")).headers.get("x-sloppy-output-cache"), "BYPASS");
        });

        await runInvariant(collector, "output-cache.hit-equivalence", async () => {
            const first = await host.get("/products/2");
            const second = await host.get("/products/2");
            assert.equal(second.headers.get("x-sloppy-output-cache"), "HIT");
            assert.equal(second.status, first.status);
            assert.deepEqual(await second.json(), await first.json());
            assert.equal(second.headers.get("x-contract"), "products");
            const metrics = app.metrics.snapshot().metrics.find((metric) => metric.name === "output_cache.requests.total");
            assert.equal(metrics.series.some((series) => series.labels.route === "/products/{id:int}" && series.labels.outcome === "hit"), true);
        });

        await runInvariant(collector, "output-cache.vary-query", async () => {
            assert.deepEqual(await (await host.get("/search?category=books")).json(), { calls: 1, query: "books" });
            assert.deepEqual(await (await host.get("/search?category=books")).json(), { calls: 1, query: "books" });
            assert.deepEqual(await (await host.get("/search?category=tools")).json(), { calls: 2, query: "tools" });
        });

        await runInvariant(collector, "output-cache.vary-header", async () => {
            assert.deepEqual(await (await host.get("/localized", { headers: { "Accept-Language": "en" } })).json(), {
                calls: 1,
                language: "en",
            });
            assert.deepEqual(await (await host.get("/localized", { headers: { "Accept-Language": "en" } })).json(), {
                calls: 1,
                language: "en",
            });
            assert.deepEqual(await (await host.get("/localized", { headers: { "Accept-Language": "ka" } })).json(), {
                calls: 2,
                language: "ka",
            });
        });

        await runInvariant(collector, "output-cache.auth-safety", async () => {
            assert.equal((await host.get("/unsafe-me", { user: user("a") })).headers.get("x-sloppy-output-cache"), "BYPASS");
            assert.deepEqual(await (await host.get("/me", { user: user("a") })).json(), {
                calls: 1,
                sub: "a",
                secret: SECRET_VALUE,
            });
            assert.deepEqual(await (await host.get("/me", { user: user("a") })).json(), {
                calls: 1,
                sub: "a",
                secret: SECRET_VALUE,
            });
            assert.deepEqual(await (await host.get("/me", { user: user("b") })).json(), {
                calls: 2,
                sub: "b",
                secret: SECRET_VALUE,
            });
        });

        await runInvariant(collector, "output-cache.set-cookie-safety", async () => {
            assert.equal((await host.get("/cookie")).headers.get("x-sloppy-output-cache"), "BYPASS");
            assert.equal((await host.get("/cookie")).headers.get("x-sloppy-output-cache"), "BYPASS");
        });

        await runInvariant(collector, "cache.tag-invalidation", async () => {
            await cache.invalidateTag("products");
            const response = await host.get("/products/1");
            assert.equal(response.headers.get("x-sloppy-output-cache"), "MISS");
        });

        await runInvariant(collector, "cache.diagnostics.redacted", async () => {
            assert.equal((await host.get("/problem")).headers.get("x-sloppy-output-cache"), "BYPASS");
            assertNoSecret(host.diagnostics.snapshot());
            assertNoSecret(cache.stats());
            assertNoSecret(app.metrics.snapshot());
        });
    } finally {
        await host.close();
    }
}

async function validateRedis(collector) {
    await runInvariant(collector, "redis.no-silent-memory-fallback", async () => {
        const restore = installBridge({
            connect() {
                return Promise.reject(new Error("redis unavailable"));
            },
        });
        try {
            const cache = Cache.redis("contract-redis", {
                url: `redis://:${REDIS_SECRET}@localhost/0`,
                connectTimeoutMs: 20,
                commandTimeoutMs: 20,
            });
            assert.equal(cache.kind, "redis");
            await assert.rejects(() => cache.set("key", { ok: true }), /SLOPPY_E_REDIS_CONNECT_FAILED/u);
            await cache.dispose();
        } finally {
            restore();
        }
    });

    await runInvariant(collector, "redis.lock-owner", async () => {
        await withRedisBridge(async (bridge) => {
            const redis = Redis.client("contract-locks", {
                url: `redis://:${REDIS_SECRET}@localhost/0`,
                commandTimeoutMs: 100,
                pool: { maxConnections: 1, pendingQueueLimit: 4 },
            });
            const locks = Redis.locks(redis, { prefix: "contracts:locks:" });
            const lease = await locks.acquire("leader", { ttlMs: 100 });
            assert.equal([...bridge.values.keys()].some((key) => key.includes("leader")), true);
            bridge.forceNextReleaseFailure();
            await assert.rejects(() => lease.extend(100), /SLOPPY_E_REDIS_LOCK_LOST/u);
            bridge.values.clear();
            const takeover = await locks.acquire("leader", { ttlMs: 100 });
            assert.equal(await takeover.release(), true);
            await redis.dispose();
        });
    });

    await runInvariant(collector, "cache.set-get", async () => {
        await withRedisBridge(async () => {
            const redis = Redis.client("contract-cache", {
                url: `redis://:${REDIS_SECRET}@localhost/0`,
                commandTimeoutMs: 100,
                pool: { maxConnections: 1, pendingQueueLimit: 4 },
            });
            const cache = Cache.redis("contract-cache", { client: redis, prefix: "contracts:cache:", ttlMs: 1000 });
            await cache.set("redis-value", { ok: true }, { tags: ["redis"] });
            assert.deepEqual(await cache.get("redis-value"), { ok: true });
            assert.equal(await cache.invalidateTag("redis"), 1);
            assert.equal(await cache.get("redis-value"), undefined);
            assertNoSecret(redis.diagnostics(), REDIS_SECRET);
            assertNoSecret(await redis.health(), REDIS_SECRET);
            await cache.dispose();
            await redis.dispose();
        });
    });
}

async function validateNegativeProbes(findings) {
    const probes = [
        ["cache.ttl-expiry", async () => assert.equal("stale-value", undefined)],
        ["cache.tag-invalidation", async () => assert.equal("tagged-value", undefined)],
        ["output-cache.auth-safety", async () => assert.deepEqual({ sub: "a" }, { sub: "b" })],
        ["output-cache.set-cookie-safety", async () => assert.equal("HIT", "BYPASS")],
        ["redis.no-silent-memory-fallback", async () => assert.fail("Redis unavailable fell back to memory")],
        ["redis.lock-owner", async () => assert.fail("non-owner released lock")],
        ["cache.diagnostics.redacted", async () => assertNoSecret({ message: SECRET_VALUE })],
    ];
    for (const [invariant, probe] of probes) {
        findings.push(expectedFailureFinding(invariant, await expectInvariantFailure(invariant, probe)));
    }
}

export async function runCacheContract({ tier }) {
    const startedAt = new Date().toISOString();
    const memoryCollector = new ContractAssertionCollector({ subsystem: SUBSYSTEM, fixture: "memory-pr" });
    const outputCollector = new ContractAssertionCollector({ subsystem: SUBSYSTEM, fixture: "output-cache-pr" });
    const redisCollector = new ContractAssertionCollector({ subsystem: SUBSYSTEM, fixture: "redis-pr" });
    const findings = [];

    await validateMemoryCache(memoryCollector);
    await validateOutputCache(outputCollector);
    await validateRedis(redisCollector);
    findings.push(...memoryCollector.findings, ...outputCollector.findings, ...redisCollector.findings);
    await validateNegativeProbes(findings);

    if (tier !== "pr") {
        const liveCollector = new ContractAssertionCollector({ subsystem: SUBSYSTEM, fixture: "redis-live-provider" });
        liveCollector.unavailable("redis.live-provider", "live Redis provider contract is reserved for an extended/live-provider lane");
        findings.push(...liveCollector.findings);
    }

    return createReport({
        subsystem: SUBSYSTEM,
        tier,
        startedAt,
        finishedAt: new Date().toISOString(),
        findings,
    });
}
