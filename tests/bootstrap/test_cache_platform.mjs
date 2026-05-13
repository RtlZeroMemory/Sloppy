import assert from "node:assert/strict";

import { Cache, Health, Results, Schema, Sloppy, TestHost, data } from "../../stdlib/sloppy/index.js";
import { Cache as CacheSubpath } from "../../stdlib/sloppy/cache.js";

async function assertRejectsMessage(fn, expected) {
    await assert.rejects(fn, (error) => {
        assert.match(String(error.message), expected);
        return true;
    });
}

function user(sub, roles = []) {
    return Object.freeze({
        authenticated: true,
        sub,
        roles: Object.freeze(roles),
        claims: Object.freeze({ sub }),
        hasRole() {
            return roles.includes(...arguments);
        },
        hasClaim(claim) {
            return claim === "sub";
        },
        claim(claim) {
            return this.claims[claim];
        },
    });
}

function createCacheBridge() {
    const stores = new Map();
    let nextSlot = 1;
    function storeFor(handle) {
        let store = stores.get(handle.slot);
        if (store === undefined) {
            store = new Map();
            stores.set(handle.slot, store);
        }
        return store;
    }
    function entryKey(namespace, key) {
        return `${namespace}\0${key}`;
    }
    function execSql(provider, handle, text, params) {
        const store = storeFor(handle);
        const sql = text.toLowerCase();
        if (sql.includes("create table")) {
            return { affectedRows: 0 };
        }
        if (sql.includes("alter table") || sql.includes("col_length")) {
            return { affectedRows: 0 };
        }
        if (sql.startsWith("insert into") || sql.includes(" on conflict ")) {
            const [namespace, key, valueJson, createdAt, updatedAt, expiresAt, absoluteExpiresAt, slidingExpirationMs, tagsJson] = params;
            store.set(entryKey(namespace, key), {
                cache_key: key,
                value_json: valueJson,
                created_at: createdAt,
                updated_at: updatedAt,
                expires_at: expiresAt,
                absolute_expires_at: absoluteExpiresAt,
                sliding_expiration_ms: slidingExpirationMs,
                tags_json: tagsJson,
            });
            return { affectedRows: 1 };
        }
        if (provider === "sqlserver" && sql.startsWith("update ")) {
            const [valueJson, updatedAt, expiresAt, absoluteExpiresAt, slidingExpirationMs, tagsJson, namespace, key] = params;
            const currentKey = entryKey(namespace, key);
            const existing = store.get(currentKey);
            if (existing === undefined) {
                return { affectedRows: 0 };
            }
            store.set(currentKey, {
                ...existing,
                value_json: valueJson,
                updated_at: updatedAt,
                expires_at: expiresAt,
                absolute_expires_at: absoluteExpiresAt,
                sliding_expiration_ms: slidingExpirationMs,
                tags_json: tagsJson,
            });
            return { affectedRows: 1 };
        }
        if (sql.startsWith("delete") && sql.includes("cache_key")) {
            const removed = store.delete(entryKey(params[0], params[1]));
            return { affectedRows: removed ? 1 : 0 };
        }
        if (sql.startsWith("delete") && sql.includes("expires_at")) {
            let removed = 0;
            for (const [key, entry] of store) {
                if (entry.expires_at !== null && entry.expires_at <= params[0]) {
                    store.delete(key);
                    removed += 1;
                }
            }
            return { affectedRows: removed };
        }
        if (sql.startsWith("delete") && sql.includes("where namespace")) {
            let removed = 0;
            for (const key of [...store.keys()]) {
                if (key.startsWith(`${params[0]}\0`)) {
                    store.delete(key);
                    removed += 1;
                }
            }
            return { affectedRows: removed };
        }
        if (sql.startsWith("delete")) {
            const removed = store.size;
            store.clear();
            return { affectedRows: removed };
        }
        throw new Error(`unexpected ${provider} cache SQL: ${text}`);
    }
    function querySql(provider, handle, text, params) {
        const store = storeFor(handle);
        const sql = text.toLowerCase();
        if (sql.includes("select cache_key")) {
            return [...store.entries()]
                .filter(([key]) => key.startsWith(`${params[0]}\0`))
                .map(([, entry]) => ({ cache_key: entry.cache_key, tags_json: entry.tags_json }));
        }
        if (sql.includes("where namespace") && sql.includes("cache_key")) {
            const row = store.get(entryKey(params[0], params[1]));
            return row === undefined ? [] : [{ ...row }];
        }
        throw new Error(`unexpected ${provider} cache query: ${text}`);
    }
    function provider(name) {
        return {
            open() {
                return { slot: nextSlot++, provider: name };
            },
            query(handle, text, params) {
                return querySql(name, handle, text, params);
            },
            queryOne(handle, text, params) {
                return querySql(name, handle, text, params)[0] ?? null;
            },
            exec(handle, text, params) {
                return execSql(name, handle, text, params);
            },
            close() {},
        };
    }
    return {
        data: {
            sqlite: provider("sqlite"),
            postgres: provider("postgres"),
            sqlserver: provider("sqlserver"),
        },
    };
}

async function withCacheBridge(callback) {
    const previous = globalThis.__sloppy;
    globalThis.__sloppy = createCacheBridge();
    try {
        return await callback();
    } finally {
        if (previous === undefined) {
            delete globalThis.__sloppy;
        } else {
            globalThis.__sloppy = previous;
        }
    }
}

{
    assert.equal(Cache, CacheSubpath);
    assert.equal(Cache.token("main"), "cache.main");
    assert.equal(Cache.token(" Main Cache "), "cache.main-cache");
    assert.equal(Cache.key("users", 42), "users:42");
    assert.deepEqual(Cache.tags("users", ["user:42"]), ["users", "user:42"]);
    assert.throws(() => Cache.memory({ maxEntries: 0 }), /maxEntries/);
    await assertRejectsMessage(() => Cache.memory({ maxEntries: 1 }).set("", 1), /cache key/);
    const noop = Cache.noop();
    await assertRejectsMessage(() => noop.get(""), /cache key/);
    noop.dispose();
    await assertRejectsMessage(() => noop.set("after-dispose", 1), /disposed/);
}

{
    const cache = Cache.memory("main", { maxEntries: 2, ttlMs: 20 });
    await cache.set("a", { value: 1 }, { tags: ["alpha"] });
    assert.deepEqual(await cache.get("a"), { value: 1 });
    const value = await cache.get("a");
    value.value = 99;
    assert.deepEqual(await cache.get("a"), { value: 1 });
    await cache.set("b", 2);
    await cache.set("c", 3);
    assert.equal(await cache.get("a"), undefined);
    assert.deepEqual(cache.stats().evictions, 1);
    await cache.set("tagged", { ok: true }, { tags: ["group"] });
    assert.equal(await cache.invalidateTag("group"), 1);
    assert.equal(await cache.get("tagged"), undefined);
    await cache.set("typed", { id: 1 }, { schema: Schema.object({ id: Schema.integer() }) });
    assert.deepEqual(await cache.get("typed", Schema.object({ id: Schema.integer() })), { id: 1 });
    await assertRejectsMessage(
        () => cache.get("typed", Schema.object({ id: Schema.string() })),
        /schema validation/,
    );
    await assertRejectsMessage(
        () => cache.set("too-large", { value: "abcdef" }, { maxValueBytes: 2 }),
        /SLOPPY_E_CACHE_VALUE_TOO_LARGE|maxValueBytes/,
    );
    const ttlCache = Cache.memory({ maxEntries: 10, ttlMs: 20 });
    await ttlCache.set("short", "lived");
    await new Promise((resolve) => setTimeout(resolve, 25));
    assert.equal(await ttlCache.get("short"), undefined);
    assert.equal(ttlCache.stats().expired >= 1, true);

    let current = Date.parse("2026-05-13T00:00:00.000Z");
    const clock = {
        now() {
            return new Date(current);
        },
        monotonicNowMs() {
            return 1;
        },
    };
    const sliding = Cache.memory({ maxEntries: 10, clock });
    await sliding.set("bounded", "value", {
        absoluteExpiration: new Date(current + 50),
        slidingExpirationMs: 100,
    });
    current += 40;
    assert.equal(await sliding.get("bounded"), "value");
    current += 20;
    assert.equal(await sliding.get("bounded"), undefined);
}

{
    const cache = Cache.memory({ maxEntries: 10 });
    let factories = 0;
    const [left, right] = await Promise.all([
        cache.getOrCreate("coalesce", { ttlMs: 1000 }, async () => {
            factories += 1;
            await new Promise((resolve) => setTimeout(resolve, 10));
            return { ok: true };
        }),
        cache.getOrCreate("coalesce", { ttlMs: 1000 }, async () => {
            factories += 1;
            return { ok: false };
        }),
    ]);
    assert.deepEqual(left, { ok: true });
    assert.deepEqual(right, { ok: true });
    assert.equal(factories, 1);
    assert.equal(cache.stats().stampedeWaiters, 1);
    await assertRejectsMessage(
        () => cache.getOrCreate("bad", { schema: Schema.object({ ok: Schema.boolean() }) }, () => ({ ok: "no" })),
        /schema validation/,
    );
    assert.equal(await cache.get("bad"), undefined);
}

{
    await withCacheBridge(async () => {
        const sqliteDb = data.sqlite.open({ database: ":memory:", capability: "data.main" });
        const postgresDb = data.postgres.open({ connectionString: "postgres://localhost/sloppy" });
        const sqlServerDb = data.sqlserver.open({ connectionString: "Driver={ODBC Driver 18 for SQL Server};Server=localhost;" });
        const providers = [
            Cache.sqlite(sqliteDb, { name: "sqlite", namespace: "sqlite" }),
            Cache.postgres(postgresDb, { name: "postgres", namespace: "postgres" }),
            Cache.sqlServer(sqlServerDb, { name: "sqlserver", namespace: "sqlserver" }),
        ];
        for (const cache of providers) {
            await cache.set("row", { value: cache.kind }, { tags: ["group"], ttlMs: 1000 });
            assert.deepEqual(await cache.get("row"), { value: cache.kind });
            assert.equal(await cache.has("row"), true);
            assert.equal(await cache.invalidateTag("group"), 1);
            assert.equal(await cache.get("row"), undefined);
            await cache.set("cleanup", { ok: true }, { absoluteExpiration: "2000-01-01T00:00:00.000Z" });
            assert.equal(await cache.get("cleanup"), undefined);
            await cache.clear();
            assert.equal(await cache.get("row"), undefined);
        }
        assert.throws(
            () => Cache.sqlite(data.createFakeProvider({ query: () => [], exec: () => ({ affectedRows: 0 }) })),
            /requires a real sqlite/,
        );
        assert.throws(
            () => Cache.postgres(data.createFakeProvider({ query: () => [], exec: () => ({ affectedRows: 0 }) })),
            /requires a real postgres/,
        );
        assert.throws(
            () => Cache.sqlServer(data.createFakeProvider({ query: () => [], exec: () => ({ affectedRows: 0 }) })),
            /requires a real sqlserver/,
        );
        const spoofedProvider = Object.freeze({
            __debug() {
                return Object.freeze({ kind: "sqlite-connection" });
            },
            query() {
                return [];
            },
            queryOne() {
                return null;
            },
            exec() {
                return { affectedRows: 0 };
            },
        });
        assert.throws(() => Cache.sqlite(spoofedProvider), /requires a real sqlite/);

        const memory = Cache.memory("front", { maxEntries: 10 });
        const distributed = Cache.sqlite(sqliteDb, { name: "back", namespace: "hybrid" });
        const hybrid = Cache.hybrid("main", { memory, distributed });
        await distributed.set("only-back", { value: 1 }, { tags: ["hybrid"] });
        assert.deepEqual(await hybrid.get("only-back"), { value: 1 });
        assert.equal(hybrid.stats().gets, 1);
        assert.deepEqual(await memory.get("only-back"), { value: 1 });
        await hybrid.set("both", { value: 2 }, { tags: ["hybrid"] });
        assert.deepEqual(await memory.get("both"), { value: 2 });
        assert.deepEqual(await distributed.get("both"), { value: 2 });
        assert.equal(await hybrid.invalidateTag("hybrid"), 4);
        assert.equal(await hybrid.get("both"), undefined);
        assert.equal(hybrid.stats().memory.kind, "memory");

        let current = Date.parse("2026-05-13T00:00:00.000Z");
        const clock = {
            now() {
                return new Date(current);
            },
        };
        const expiringMemory = Cache.memory("expiring-front", { maxEntries: 10, clock });
        const expiringDistributed = Cache.sqlite(sqliteDb, { name: "expiring-back", namespace: "hybrid-expiring", clock });
        const expiringHybrid = Cache.hybrid("expiring-main", {
            memory: expiringMemory,
            distributed: expiringDistributed,
        });
        await expiringDistributed.set("bounded", { value: 3 }, {
            absoluteExpiration: new Date(current + 50),
            slidingExpirationMs: 100,
        });
        current += 40;
        assert.deepEqual(await expiringHybrid.get("bounded"), { value: 3 });
        current += 20;
        assert.equal(await expiringMemory.get("bounded"), undefined);
        assert.equal(await expiringHybrid.get("bounded"), undefined);

        let memoryDisposed = false;
        let distributedDisposed = false;
        const asyncMemory = Cache.memory("async-front", { maxEntries: 10 });
        const memoryPrototype = Object.getPrototypeOf(asyncMemory);
        const originalMemoryDispose = memoryPrototype.dispose;
        memoryPrototype.dispose = async function dispose() {
            await Promise.resolve();
            if (this === asyncMemory) {
                memoryDisposed = true;
            }
            originalMemoryDispose.call(this);
        };
        const asyncDistributed = Cache.sqlite(sqliteDb, { name: "async-back", namespace: "async-hybrid" });
        const distributedPrototype = Object.getPrototypeOf(asyncDistributed);
        const originalDistributedDispose = distributedPrototype.dispose;
        distributedPrototype.dispose = async function dispose() {
            await Promise.resolve();
            if (this === asyncDistributed) {
                distributedDisposed = true;
            }
            return originalDistributedDispose.call(this);
        };
        try {
            await Cache.hybrid("async-main", { memory: asyncMemory, distributed: asyncDistributed }).dispose();
            assert.equal(memoryDisposed, true);
            assert.equal(distributedDisposed, true);
        } finally {
            memoryPrototype.dispose = originalMemoryDispose;
            distributedPrototype.dispose = originalDistributedDispose;
        }

        let failureMemoryDisposed = false;
        let failureDistributedDisposed = false;
        const failureMemory = Cache.memory("async-fail-front", { maxEntries: 10 });
        const failureDistributed = Cache.sqlite(sqliteDb, { name: "async-fail-back", namespace: "async-hybrid-fail" });
        memoryPrototype.dispose = async function dispose() {
            await Promise.resolve();
            if (this === failureMemory) {
                failureMemoryDisposed = true;
            }
            return originalMemoryDispose.call(this);
        };
        distributedPrototype.dispose = async function dispose() {
            await Promise.resolve();
            if (this === failureDistributed) {
                failureDistributedDisposed = true;
                throw new Error("distributed dispose failed");
            }
            return originalDistributedDispose.call(this);
        };
        try {
            await assert.rejects(
                () => Cache.hybrid("async-fail-main", { memory: failureMemory, distributed: failureDistributed }).dispose(),
                /distributed dispose failed/u,
            );
            assert.equal(failureMemoryDisposed, true);
            assert.equal(failureDistributedDisposed, true);
        } finally {
            memoryPrototype.dispose = originalMemoryDispose;
            distributedPrototype.dispose = originalDistributedDispose;
        }
    });
}

{
    const app = Sloppy.create();
    const cache = Cache.memory("default", { maxEntries: 100 });
    app.services.addCache(cache);
    let calls = 0;
    app.get("/products", () => {
        calls += 1;
        return Results.json({ calls });
    }).outputCache({
        ttlMs: 1000,
        varyByQuery: ["category"],
        tags: ["products"],
    });
    const host = await TestHost.create(app);
    const first = await host.get("/products?category=books");
    const second = await host.get("/products?category=books");
    const other = await host.get("/products?category=tools");
    assert.equal(first.headers.get("x-sloppy-output-cache"), "MISS");
    assert.equal(second.headers.get("x-sloppy-output-cache"), "HIT");
    assert.equal(other.headers.get("x-sloppy-output-cache"), "MISS");
    assert.deepEqual(await second.json(), { calls: 1 });
    assert.deepEqual(await other.json(), { calls: 2 });
    await cache.invalidateTag("products");
    const afterInvalidation = await host.get("/products?category=books");
    assert.equal(afterInvalidation.headers.get("x-sloppy-output-cache"), "MISS");
    assert.deepEqual(await afterInvalidation.json(), { calls: 3 });
    const metricNames = app.metrics.snapshot().metrics.map((metric) => metric.name);
    assert.equal(metricNames.includes("cache.gets.total"), true);
    assert.equal(metricNames.includes("cache.hits.total"), true);
    assert.equal(metricNames.includes("output_cache.requests.total"), true);
    const outputMetric = app.metrics.snapshot().metrics.find((metric) => metric.name === "output_cache.requests.total");
    assert.equal(outputMetric.series.some((series) => series.labels.route === "/products" && series.labels.outcome === "hit"), true);
    assert.equal(outputMetric.series.every((series) => !Object.hasOwn(series.labels, "url") && !Object.hasOwn(series.labels, "user")), true);
    assert.equal(host.diagnostics.snapshot().some((entry) => entry.code === "SLOPPY_OUTPUT_CACHE_HIT" && entry.fields.key === undefined), true);
    await host.close();
}

{
    const app = Sloppy.create();
    app.services.addCache(Cache.memory({ maxEntries: 100 }));
    app.get("/set-cookie", () => Results.json({ ok: true }).cookie("sid", "secret"))
        .outputCache({ ttlMs: 1000 });
    app.post("/mutate", () => Results.json({ ok: true }))
        .outputCache({ ttlMs: 1000 });
    app.get("/too-large", () => Results.text("abcdef"))
        .outputCache({ ttlMs: 1000, maxBodyBytes: 2 });
    app.get("/utf8-text", () => Results.text("é"))
        .outputCache({ ttlMs: 1000, maxBodyBytes: 1 });
    app.get("/utf8-json", () => Results.json({ value: "é" }))
        .outputCache({ ttlMs: 1000, maxBodyBytes: 13 });
    app.get("/ascii", () => Results.text("abc"))
        .outputCache({ ttlMs: 1000, maxBodyBytes: 3 });
    app.get("/html", () => Results.html("<strong>asset</strong>"))
        .outputCache({ ttlMs: 1000 });
    app.get("/problem", () => Results.problem("broken", { status: 500 }))
        .outputCache({ ttlMs: 1000, statusCodes: [500] });
    app.get("/stream", () => Results.stream((writer) => writer.writeText("hello")))
        .outputCache({ ttlMs: 1000 });
    app.get("/function-body", () => Object.freeze({ __sloppyResult: true, kind: "json", status: 200, body: { fn() {} } }))
        .outputCache({ ttlMs: 1000 });
    const host = await TestHost.create(app);
    assert.equal((await host.get("/set-cookie")).headers.get("x-sloppy-output-cache"), "BYPASS");
    assert.equal((await host.post("/mutate")).headers.get("x-sloppy-output-cache"), "BYPASS");
    assert.equal((await host.get("/too-large")).headers.get("x-sloppy-output-cache"), "BYPASS");
    assert.equal((await host.get("/utf8-text")).headers.get("x-sloppy-output-cache"), "BYPASS");
    assert.equal((await host.get("/utf8-json")).headers.get("x-sloppy-output-cache"), "BYPASS");
    assert.equal((await host.get("/ascii")).headers.get("x-sloppy-output-cache"), "MISS");
    assert.equal((await host.get("/ascii")).headers.get("x-sloppy-output-cache"), "HIT");
    assert.equal((await host.get("/html")).headers.get("x-sloppy-output-cache"), "BYPASS");
    assert.equal((await host.get("/problem")).headers.get("x-sloppy-output-cache"), "BYPASS");
    assert.equal((await host.get("/stream")).headers.get("x-sloppy-output-cache"), "BYPASS");
    await assert.rejects(() => host.get("/function-body"), /cannot serialize function/);
    assert.equal(host.diagnostics.snapshot().some((entry) =>
        entry.code === "SLOPPY_OUTPUT_CACHE_BYPASS" && entry.fields.reason === "unsupported-body"), true);
    await host.close();
}

{
    const app = Sloppy.create();
    let calls = 0;
    app.get("/overlay", () => {
        calls += 1;
        return Results.json({ calls });
    }).outputCache({ ttlMs: 1000 });
    const host = await TestHost.create(app, { caches: { default: Cache.memory({ maxEntries: 10 }) } });
    assert.equal((await host.get("/overlay")).headers.get("x-sloppy-output-cache"), "MISS");
    assert.equal((await host.get("/overlay")).headers.get("x-sloppy-output-cache"), "HIT");
    assert.equal(calls, 1);
    await host.close();
}

{
    const app = Sloppy.create();
    app.services.addCache(Cache.memory({ maxEntries: 100 }));
    let calls = 0;
    app.get("/me/dashboard", (ctx) => {
        calls += 1;
        return Results.json({ calls, sub: ctx.user.sub });
    }).requiresAuth().outputCache({
        ttlMs: 1000,
        varyByUser: true,
    });
    app.get("/unsafe", () => Results.json({ ok: true })).requiresAuth().outputCache({ ttlMs: 1000 });
    app.get("/role-unsafe", () => Results.json({ ok: true })).requiresAuth().outputCache({
        ttlMs: 1000,
        varyByRole: true,
    });
    let sharedCalls = 0;
    app.get("/role-shared", () => {
        sharedCalls += 1;
        return Results.json({ calls: sharedCalls });
    }).requiresAuth().outputCache({
        ttlMs: 1000,
        varyByRole: true,
        allowSharedAuthenticated: true,
    });
    const host = await TestHost.create(app);
    const userA = user("a");
    const userB = user("b");
    const adminA = user("admin-a", ["admin"]);
    const adminB = user("admin-b", ["admin"]);
    const ops = user("ops", ["ops"]);
    assert.equal((await host.get("/me/dashboard", { user: userA })).headers.get("x-sloppy-output-cache"), "MISS");
    assert.equal((await host.get("/me/dashboard", { user: userA })).headers.get("x-sloppy-output-cache"), "HIT");
    assert.deepEqual(await (await host.get("/me/dashboard", { user: userB })).json(), { calls: 2, sub: "b" });
    assert.equal((await host.get("/unsafe", { user: userA })).headers.get("x-sloppy-output-cache"), "BYPASS");
    assert.equal((await host.get("/role-unsafe", { user: adminA })).headers.get("x-sloppy-output-cache"), "BYPASS");
    assert.equal((await host.get("/role-shared", { user: adminA })).headers.get("x-sloppy-output-cache"), "MISS");
    assert.equal((await host.get("/role-shared", { user: adminB })).headers.get("x-sloppy-output-cache"), "HIT");
    assert.equal((await host.get("/role-shared", { user: ops })).headers.get("x-sloppy-output-cache"), "MISS");
    await host.close();
}

{
    const app = Sloppy.create();
    app.get("/asset", () => Results.text("asset")).cacheHeaders({
        cacheControl: "public, max-age=60",
        vary: ["Accept-Encoding"],
        etag: true,
    });
    const host = await TestHost.create(app);
    const response = await host.get("/asset");
    assert.equal(response.headers.get("cache-control"), "public, max-age=60");
    assert.equal(response.headers.get("vary"), "Accept-Encoding");
    assert.match(response.headers.get("etag"), /^".+"$/);
    await host.close();

    const result = Results.json({ ok: true })
        .cacheControl("private, max-age=30")
        .cacheHeaders({ vary: ["Accept-Language"], etag: true });
    assert.equal(result.headers["Cache-Control"], "private, max-age=30");
    assert.equal(result.headers.Vary, "Accept-Language");
    assert.match(result.headers.ETag, /^".+"$/);
}

{
    const cache = Cache.memory("health", { maxEntries: 10 });
    assert.equal(Health.cache(null)().status, "degraded");
    assert.equal(Health.cache(cache)().status, "healthy");
    cache.dispose();
    assert.equal(Health.cache(cache)().status, "unhealthy");
}
