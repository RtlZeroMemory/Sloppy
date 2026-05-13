import assert from "node:assert/strict";

import { Health, RateLimit, Results, Sloppy, SloppyRateLimitError, TestHost } from "../../stdlib/sloppy/index.js";

function user(sub, extra = {}) {
    return Object.freeze({
        authenticated: true,
        sub,
        name: sub,
        roles: Object.freeze(extra.roles ?? []),
        scopes: Object.freeze(extra.scopes ?? []),
        claims: Object.freeze({ sub, ...(extra.claims ?? {}) }),
        scheme: extra.scheme ?? "bearerAuth",
        authScheme: extra.authScheme ?? extra.scheme ?? "bearerAuth",
        hasRole(role) {
            return this.roles.includes(role);
        },
        hasScope(scope) {
            return this.scopes.includes(scope);
        },
        hasClaim(name, value = undefined) {
            return Object.prototype.hasOwnProperty.call(this.claims, name) &&
                (value === undefined || Object.is(this.claims[name], value));
        },
    });
}

async function assertRejectsMessage(fn, pattern) {
    await assert.rejects(fn, pattern);
}

assert.equal(typeof RateLimit.fixedWindow, "function");
assert.equal(typeof RateLimit.slidingWindow, "function");
assert.equal(typeof RateLimit.tokenBucket, "function");
assert.equal(typeof RateLimit.concurrency, "function");
assert.equal(typeof RateLimit.memory, "function");
assert.equal(typeof RateLimit.redis, "function");
assert.equal(typeof RateLimit.partition.ip, "function");
assert.equal(typeof SloppyRateLimitError, "function");

assert.throws(() => RateLimit.fixedWindow({ limit: 0, windowMs: 1000, partitionBy: RateLimit.partition.ip() }), /limit/);
assert.throws(() => RateLimit.fixedWindow({ limit: 1, windowMs: 0, partitionBy: RateLimit.partition.ip() }), /windowMs/);
assert.throws(() => RateLimit.fixedWindow({ limit: 1, windowMs: 1000 }), /partitionBy/);
assert.throws(() => RateLimit.partition.header("bad header"), /HTTP token/);

const policy = RateLimit.fixedWindow({
    name: "login",
    limit: 2,
    windowMs: 1000,
    partitionBy: RateLimit.partition.ip(),
});
assert.equal(Object.isFrozen(policy), true);
assert.equal(policy.metadata.algorithm, "fixedWindow");
assert.deepEqual(policy.metadata.partition.kind, "ip");

{
    const app = Sloppy.create();
    let calls = 0;
    app.post("/login", () => {
        calls += 1;
        return Results.ok({ ok: true });
    }).rateLimit(policy);
    const host = await TestHost.create(app);
    await host.post("/login", { remoteAddress: "203.0.113.10" }).then((response) => response.expectStatus(200));
    await host.post("/login", { remoteAddress: "203.0.113.10" }).then((response) => response.expectStatus(200));
    const denied = await host.post("/login", { remoteAddress: "203.0.113.10" });
    denied
        .expectStatus(429)
        .expectHeader("Retry-After", "1")
        .expectHeader("RateLimit-Limit", "2")
        .expectHeader("RateLimit-Remaining", "0")
        .expectHeader("RateLimit-Reset", "1")
        .expectProblem({
            status: 429,
            code: "SLOPPY_E_RATE_LIMIT_EXCEEDED",
            title: "Too Many Requests",
        });
    assert.equal(calls, 2);
    assert.equal(denied.text().includes("203.0.113.10"), false);
    host.diagnostics.expectCode("SLOPPY_E_RATE_LIMIT_EXCEEDED").expectNoSecretLeaks();
}

{
    const app = Sloppy.create();
    app.get("/xff-safe", () => Results.ok({ ok: true })).rateLimit(RateLimit.fixedWindow({
        limit: 1,
        windowMs: 60_000,
        partitionBy: RateLimit.partition.ip(),
    }));
    const host = await TestHost.create(app);
    await host.get("/xff-safe", {
        remoteAddress: "203.0.113.77",
        headers: { "x-forwarded-for": "198.51.100.1" },
    }).then((response) => response.expectStatus(200));
    await host.get("/xff-safe", {
        remoteAddress: "203.0.113.77",
        headers: { "x-forwarded-for": "198.51.100.2" },
    }).then((response) => response.expectStatus(429));
}

{
    const app = Sloppy.create();
    app.get("/xff-trusted", () => Results.ok({ ok: true })).rateLimit(RateLimit.fixedWindow({
        limit: 1,
        windowMs: 60_000,
        partitionBy: RateLimit.partition.ip({ trustProxy: true }),
    }));
    const host = await TestHost.create(app);
    await host.get("/xff-trusted", {
        remoteAddress: "203.0.113.10",
        headers: { "x-forwarded-for": "198.51.100.10, 203.0.113.10" },
    }).then((response) => response.expectStatus(200));
    await host.get("/xff-trusted", {
        remoteAddress: "203.0.113.10",
        headers: { "x-forwarded-for": "198.51.100.11, 203.0.113.10" },
    }).then((response) => response.expectStatus(200));
    await host.get("/xff-trusted", {
        remoteAddress: "203.0.113.10",
        headers: { "x-forwarded-for": "198.51.100.10, 203.0.113.10" },
    }).then((response) => response.expectStatus(429));
}

{
    const app = Sloppy.create();
    app.get("/selector-api-key", () => Results.ok({ ok: true })).rateLimit(RateLimit.fixedWindow({
        name: "selector-shared",
        limit: 1,
        windowMs: 60_000,
        partitionBy: RateLimit.partition.header("x-api-key"),
    }));
    app.get("/selector-tenant", () => Results.ok({ ok: true })).rateLimit(RateLimit.fixedWindow({
        name: "selector-shared",
        limit: 1,
        windowMs: 60_000,
        partitionBy: RateLimit.partition.header("x-tenant"),
    }));
    const host = await TestHost.create(app, { secrets: { selector: "same-value" } });
    await host.get("/selector-api-key", { headers: { "x-api-key": "same-value" } }).then((response) => response.expectStatus(200));
    await host.get("/selector-tenant", { headers: { "x-tenant": "same-value" } }).then((response) => response.expectStatus(200));
    await host.get("/selector-api-key", { headers: { "x-api-key": "same-value" } }).then((response) => response.expectStatus(429));
    const diagnostics = JSON.stringify(host.diagnostics.snapshot());
    assert.equal(diagnostics.includes("same-value"), false);
    assert.equal(
        host.diagnostics.filter({ code: "SLOPPY_E_RATE_LIMIT_EXCEEDED" })
            .some((record) => typeof record.fields.partitionHash === "string" && record.fields.partitionHash.length > 0),
        true,
    );
}

{
    const app = Sloppy.create();
    app.get("/selector-claim", () => Results.ok({ ok: true })).requiresAuth().rateLimit(RateLimit.fixedWindow({
        name: "tenant-selector",
        limit: 1,
        windowMs: 60_000,
        partitionBy: RateLimit.partition.claim("tenant"),
    }));
    app.get("/selector-header", () => Results.ok({ ok: true })).rateLimit(RateLimit.fixedWindow({
        name: "tenant-selector",
        limit: 1,
        windowMs: 60_000,
        partitionBy: RateLimit.partition.header("tenant"),
    }));
    const host = await TestHost.create(app);
    await host.get("/selector-claim", { user: user("ada", { claims: { tenant: "acme" } }) }).then((response) => response.expectStatus(200));
    await host.get("/selector-header", { headers: { tenant: "acme" } }).then((response) => response.expectStatus(200));
}

{
    const app = Sloppy.create();
    app.get("/unnamed-a", () => Results.ok({ ok: true })).rateLimit(RateLimit.fixedWindow({
        limit: 1,
        windowMs: 60_000,
        partitionBy: RateLimit.partition.ip(),
    }));
    app.get("/unnamed-b", () => Results.ok({ ok: true })).rateLimit(RateLimit.fixedWindow({
        limit: 1,
        windowMs: 60_000,
        partitionBy: RateLimit.partition.ip(),
    }));
    app.get("/named-a", () => Results.ok({ ok: true })).rateLimit(RateLimit.fixedWindow({
        name: "shared-route-quota",
        limit: 1,
        windowMs: 60_000,
        partitionBy: RateLimit.partition.ip(),
    }));
    app.get("/named-b", () => Results.ok({ ok: true })).rateLimit(RateLimit.fixedWindow({
        name: "shared-route-quota",
        limit: 1,
        windowMs: 60_000,
        partitionBy: RateLimit.partition.ip(),
    }));
    const host = await TestHost.create(app);
    await host.get("/unnamed-a", { remoteAddress: "192.0.2.44" }).then((response) => response.expectStatus(200));
    await host.get("/unnamed-a", { remoteAddress: "192.0.2.44" }).then((response) => response.expectStatus(429));
    await host.get("/unnamed-b", { remoteAddress: "192.0.2.44" }).then((response) => response.expectStatus(200));
    await host.get("/named-a", { remoteAddress: "192.0.2.45" }).then((response) => response.expectStatus(200));
    await host.get("/named-b", { remoteAddress: "192.0.2.45" }).then((response) => response.expectStatus(429));
}

{
    const clock = {
        value: 0,
        monotonicNowMs() {
            return this.value;
        },
    };
    const app = Sloppy.create();
    app.get("/sliding", () => Results.ok({ ok: true })).rateLimit(RateLimit.slidingWindow({
        name: "sliding",
        limit: 2,
        windowMs: 1000,
        partitionBy: RateLimit.partition.header("x-api-key"),
    }));
    const host = await TestHost.create(app, { clock, secrets: { apiKey: "secret-key" } });
    await host.get("/sliding", { headers: { "x-api-key": "secret-key" } }).then((response) => response.expectStatus(200));
    await host.get("/sliding", { headers: { "x-api-key": "secret-key" } }).then((response) => response.expectStatus(200));
    await host.get("/sliding", { headers: { "x-api-key": "secret-key" } }).then((response) => response.expectStatus(429));
    clock.value = 1001;
    await host.get("/sliding", { headers: { "x-api-key": "secret-key" } }).then((response) => response.expectStatus(200));
    assert.equal(JSON.stringify(host.diagnostics.snapshot()).includes("secret-key"), false);
}

{
    const clock = {
        value: 0,
        monotonicNowMs() {
            return this.value;
        },
    };
    const app = Sloppy.create();
    app.get("/bucket/{id}", () => Results.ok({ ok: true })).rateLimit(RateLimit.tokenBucket({
        name: "bucket",
        capacity: 2,
        refillPerSecond: 1,
        partitionBy: RateLimit.partition.routeParam("id"),
        cost: 1,
    }));
    const host = await TestHost.create(app, { clock });
    await host.get("/bucket/a").then((response) => response.expectStatus(200));
    await host.get("/bucket/a").then((response) => response.expectStatus(200));
    await host.get("/bucket/a").then((response) => response.expectStatus(429));
    clock.value = 1000;
    await host.get("/bucket/a").then((response) => response.expectStatus(200));
}

{
    const app = Sloppy.create();
    const group = app.group("/api").rateLimit(RateLimit.fixedWindow({
        name: "api",
        limit: 1,
        windowMs: 60_000,
        partitionBy: RateLimit.partition.user(),
    }));
    group.get("/me", () => Results.ok({ ok: true })).requiresAuth();
    const host = await TestHost.create(app);
    await host.get("/api/me", { user: user("ada") }).then((response) => response.expectStatus(200));
    await host.get("/api/me", { user: user("grace") }).then((response) => response.expectStatus(200));
    await host.get("/api/me", { user: user("ada") }).then((response) => response.expectStatus(429));
    await host.openapi.expectResponse("GET", "/api/me", 429);
    const doc = await host.openapi.snapshot();
    assert.equal(doc.paths["/api/me"].get["x-slop-rate-limit"][0].partition.kind, "user");
}

{
    const app = Sloppy.create();
    app.get("/head", () => Results.ok({ ok: true })).rateLimit(RateLimit.fixedWindow({
        name: "head",
        limit: 0 + 1,
        windowMs: 60_000,
        partitionBy: RateLimit.partition.ip(),
        problem: () => ({ title: "Slow down" }),
    }));
    const host = await TestHost.create(app);
    await host.head("/head", { remoteAddress: "198.51.100.1" }).then((response) => response.expectStatus(200).expectNoBody());
    await host.head("/head", { remoteAddress: "198.51.100.1" }).then((response) => {
        response.expectStatus(429).expectNoBody().expectHeader("Retry-After", "60");
    });
}

{
    const store = RateLimit.memory({ maxKeys: 1, rejectOnMaxKeys: true });
    const app = Sloppy.create();
    app.services.addRateLimitStore("small", store);
    app.get("/limited", () => Results.ok({ ok: true })).rateLimit(RateLimit.fixedWindow({
        name: "small",
        limit: 1,
        windowMs: 60_000,
        store: "small",
        partitionBy: RateLimit.partition.ip(),
    }));
    const host = await TestHost.create(app);
    await host.get("/limited", { remoteAddress: "10.0.0.1" }).then((response) => response.expectStatus(200));
    await assertRejectsMessage(
        () => host.get("/limited", { remoteAddress: "10.0.0.2" }),
        /SLOPPY_E_RATE_LIMIT_STORE_FULL/,
    );
    const stats = store.stats();
    assert.equal(stats.keys, 1);
    assert.equal(stats.rejectedKeys, 1);
    store.dispose();
    const health = await Health.rateLimit(store)();
    assert.equal(health.status, "unhealthy");
}

{
    const store = RateLimit.memory();
    const clockPolicy = RateLimit.concurrency({
        name: "clocked-concurrency",
        limit: 1,
        partitionBy: RateLimit.partition.custom((ctx) => ctx.partition, { marker: "clock-partition" }),
    });
    const lease = await store.check({
        policy: clockPolicy,
        cost: 1,
        partitionHash: "clock-partition",
        nowMs: 1234,
    });
    assert.equal(lease.allowed, true);
    lease.release();
    assert.equal(store.stats().keys, 0);
    const next = await store.check({
        policy: clockPolicy,
        cost: 1,
        partitionHash: "clock-partition",
        nowMs: 1,
    });
    assert.equal(next.allowed, true);
    next.release();
}

{
    const app = Sloppy.create();
    let host;
    let nestedStatus;
    app.get("/concurrent-async", async () => {
        if (nestedStatus === undefined) {
            const nested = await host.get("/concurrent-async", { remoteAddress: "198.51.100.80" });
            nestedStatus = nested.status;
        }
        return Results.ok({ ok: true });
    }).rateLimit(RateLimit.concurrency({
        limit: 1,
        partitionBy: RateLimit.partition.ip(),
    }));
    host = await TestHost.create(app);
    await host.get("/concurrent-async", { remoteAddress: "198.51.100.80" }).then((response) => response.expectStatus(200));
    assert.equal(nestedStatus, 429);
    await host.get("/concurrent-async", { remoteAddress: "198.51.100.80" }).then((response) => response.expectStatus(200));
}

{
    const app = Sloppy.create();
    let thrown = false;
    app.get("/concurrent-throw", () => {
        if (!thrown) {
            thrown = true;
            throw new Error("boom");
        }
        return Results.ok({ ok: true });
    }).rateLimit(RateLimit.concurrency({
        limit: 1,
        partitionBy: RateLimit.partition.ip(),
    }));
    const host = await TestHost.create(app);
    await assert.rejects(() => host.get("/concurrent-throw", { remoteAddress: "198.51.100.81" }), /boom/);
    await host.get("/concurrent-throw", { remoteAddress: "198.51.100.81" }).then((response) => response.expectStatus(200));
}

{
    const app = Sloppy.create();
    app.get("/concurrent-stream", () => Results.stream(async (writer) => {
        writer.writeText("event: ready\n\n");
    })).rateLimit(RateLimit.concurrency({
        limit: 1,
        partitionBy: RateLimit.partition.ip(),
    }));
    const host = await TestHost.create(app);
    await host.get("/concurrent-stream", { remoteAddress: "198.51.100.82" }).then((response) => response.expectStatus(200));
    await host.get("/concurrent-stream", { remoteAddress: "198.51.100.82" }).then((response) => response.expectStatus(200));
}

{
    const redis = RateLimit.redis(undefined, { prefix: "sloppy:rl:" });
    const redisPolicy = RateLimit.fixedWindow({
        name: "redis-unavailable",
        limit: 1,
        windowMs: 1000,
        partitionBy: RateLimit.partition.ip(),
    });
    await assertRejectsMessage(
        () => redis.check({
            policy: redisPolicy,
            cost: 1,
            partitionHash: "partition",
            nowMs: 0,
        }),
        /SLOPPY_E_RATE_LIMIT_REDIS_UNAVAILABLE/,
    );
}

{
    const app = Sloppy.create();
    let accepted = 0;
    app.websocket("/ws", async (socket) => {
        accepted += 1;
        await socket.accept();
        await socket.close();
    }).rateLimit(RateLimit.fixedWindow({
        name: "ws-connect",
        limit: 1,
        windowMs: 60_000,
        partitionBy: RateLimit.partition.ip(),
    }));
    const host = await TestHost.create(app);
    const socket = await host.websocket("/ws").connect({ remoteAddress: "203.0.113.200" });
    await socket.expectClose();
    await host.websocket("/ws").connect({ remoteAddress: "203.0.113.200" }).expectRejected(429);
    assert.equal(accepted, 1);
}
