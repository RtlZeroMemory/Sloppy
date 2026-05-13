import assert from "node:assert/strict";

import {
    RateLimit,
    Results,
    Schema,
    Sloppy,
    TestHost,
} from "../../stdlib/sloppy/index.js";

const SECRET_MARKER = "FRAMEWORK_MATRIX_SECRET_SHOULD_NOT_LEAK";

const builder = Sloppy.createBuilder();
builder.config.addObject({
    App: {
        Name: "framework-matrix",
    },
});
builder.services.addSingleton("message", Object.freeze({ value: "hello" }));

let scopedCreated = 0;
let scopedDisposed = 0;
let transientCreated = 0;
let transientDisposed = 0;
builder.services.addScoped("request-scope", () => {
    scopedCreated += 1;
    const id = scopedCreated;
    return {
        id,
        dispose() {
            scopedDisposed += 1;
        },
    };
});
builder.services.addTransient("transient-value", () => {
    transientCreated += 1;
    const id = transientCreated;
    return {
        id,
        dispose() {
            transientDisposed += 1;
        },
    };
});

const app = builder.build();
app.useErrors({ includeDetails: false, maxBodyBytes: 128 });
app.useCors({
    origins: ["https://client.example"],
    headers: ["content-type", "x-api-key"],
    methods: ["GET", "POST", "PATCH", "DELETE"],
});

const middlewareOrder = [];
app.use(async (ctx, next) => {
    middlewareOrder.push(`app:${ctx.request.method}:before`);
    const response = await next();
    middlewareOrder.push(`app:${ctx.request.method}:after`);
    return response;
});

const CreateItem = Schema.object({
    name: Schema.string().min(3),
});

const authedPolicy = RateLimit.fixedWindow({
    name: "matrix-api-ip",
    limit: 20,
    windowMs: 1000,
    partitionBy: RateLimit.partition.ip(),
});

const onePerRoute = RateLimit.fixedWindow({
    name: "matrix-limited",
    limit: 1,
    windowMs: 1000,
    partitionBy: RateLimit.partition.ip(),
});

const api = app.group("/api")
    .requiresAuth()
    .rateLimit(authedPolicy);

api.use(async (ctx, next) => {
    middlewareOrder.push(`group:${ctx.request.method}:before`);
    const response = await next();
    middlewareOrder.push(`group:${ctx.request.method}:after`);
    return response;
});

api.get("/public", () => Results.ok({ ok: true })).allowAnonymous();

api.get("/items/{id:int}", (ctx) => {
    const scoped = ctx.services.get("request-scope");
    const transientA = ctx.services.get("transient-value");
    const transientB = ctx.services.get("transient-value");
    const user = ctx.requireUser();
    return Results.json({
        id: Number(ctx.route.id),
        query: ctx.query.filter,
        header: ctx.request.headers.get("x-trace"),
        configName: ctx.config.require("App:Name"),
        singleton: ctx.services.get("message").value,
        scopedId: scoped.id,
        transientIdsDifferent: transientA.id !== transientB.id,
        user: user.sub,
        routePattern: ctx.routePattern,
        remoteAddress: ctx.connection.remoteAddress,
        connection: {
            protocol: ctx.connection.protocol,
            scheme: ctx.connection.scheme,
            secure: ctx.connection.secure,
        },
    });
}).requiresScope("items:read").requiresRole("reader");

api.post("/json", async (ctx) => {
    const input = await ctx.body.validate(CreateItem);
    return Results.status(201, { accepted: input.name });
}).accepts(CreateItem).returns(201, Schema.object({ accepted: Schema.string() }));

api.post("/text", (ctx) => Results.text(ctx.request.text()));
api.post("/bytes", (ctx) => Results.bytes(ctx.request.bytes()));
api.patch("/items/{id:int}", (ctx) => Results.ok({
    id: Number(ctx.route.id),
    patch: ctx.request.json(),
}));
api.delete("/items/{id:int}", () => Results.noContent());
api.get("/head", () => Results.text("head body", { headers: { "X-Head": "yes" } }));
api.get("/limited", () => Results.ok({ ok: true })).rateLimit(onePerRoute);
api.get("/boom", () => {
    throw new Error(SECRET_MARKER);
});

const host = await TestHost.create(app, {
    secrets: {
        marker: SECRET_MARKER,
    },
    rateLimit: {
        stores: {
            default: RateLimit.memory({ name: "framework-matrix", maxKeys: 64 }),
        },
    },
});

try {
    await host.get("/api/public")
        .expectStatus(200)
        .then((response) => response.expectJson({ ok: true }));

    const anonymous = await host.get("/api/items/42");
    anonymous.expectStatus(401).expectProblem({ status: 401, code: "SLOPPY_E_AUTH_UNAUTHORIZED" });

    const wrongRole = await host.asUser({
        sub: "user-1",
        scopes: ["items:read"],
        roles: ["writer"],
    }).get("/api/items/42");
    wrongRole.expectStatus(403).expectProblem({ status: 403, code: "SLOPPY_E_AUTH_FORBIDDEN" });

    const client = host.asUser({
        sub: "user-1",
        scopes: ["items:read"],
        roles: ["reader"],
        claims: { tenant: "core" },
    });

    middlewareOrder.length = 0;
    const first = await client.get("/api/items/42")
        .query({ filter: "recent" })
        .header("x-trace", "trace-1")
        .send();
    first.expectStatus(200).expectJson({
        id: 42,
        query: "recent",
        header: "trace-1",
        configName: "framework-matrix",
        singleton: "hello",
        scopedId: 1,
        transientIdsDifferent: true,
        user: "user-1",
        routePattern: "/api/items/{id:int}",
        remoteAddress: "test-host",
        connection: {
            protocol: "http",
            scheme: "test",
            secure: false,
        },
    });
    assert.deepEqual(middlewareOrder, [
        "app:GET:before",
        "group:GET:before",
        "group:GET:after",
        "app:GET:after",
    ]);
    assert.equal(scopedDisposed, 1);
    assert.equal(transientDisposed, 2);

    const second = await client.get("/api/items/7", { remoteAddress: "198.51.100.22" });
    second.expectStatus(200);
    assert.equal(second.json().scopedId, 2);
    assert.equal(scopedDisposed, 2);

    await client.post("/api/json")
        .json({ name: "Ada" })
        .expectStatus(201)
        .then((response) => response.expectJson({ accepted: "Ada" }));
    const invalid = await client.post("/api/json").json({ name: "Al" });
    invalid.expectStatus(400).expectProblem({ status: 400, code: "SLOPPY_E_VALIDATION_FAILED" });

    await client.post("/api/text").text("plain body")
        .then((response) => response.expectStatus(200).expectText("plain body"));
    const bytes = new Uint8Array([0, 1, 2, 253, 254, 255]);
    await client.post("/api/bytes").bytes(bytes)
        .then((response) => {
            response.expectStatus(200);
            assert.deepEqual(Array.from(response.bytes()), Array.from(bytes));
        });
    await client.patch("/api/items/42").json({ op: "rename" })
        .then((response) => response.expectStatus(200).expectJson({ id: 42, patch: { op: "rename" } }));
    await client.delete("/api/items/42").expectStatus(204);

    const head = await client.head("/api/head");
    head.expectStatus(200).expectHeader("x-head", "yes").expectNoBody();

    const preflight = await host.options("/api/items/42", {
        headers: {
            origin: "https://client.example",
            "access-control-request-method": "GET",
            "access-control-request-headers": "content-type,x-api-key",
        },
    });
    preflight.expectStatus(204)
        .expectHeader("access-control-allow-origin", "https://client.example")
        .expectNoBody();

    const limitedClient = client.withHeader("x-extra", "1");
    await limitedClient.get("/api/limited", { remoteAddress: "203.0.113.10" }).expectStatus(200);
    const denied = await limitedClient.get("/api/limited", { remoteAddress: "203.0.113.10" });
    denied.expectStatus(429)
        .expectHeader("retry-after", /^\d+$/u)
        .expectProblem({ status: 429, code: "SLOPPY_E_RATE_LIMIT_EXCEEDED" });
    await limitedClient.get("/api/limited", { remoteAddress: "203.0.113.11" }).expectStatus(200);
    const headDenied = await limitedClient.head("/api/limited", { remoteAddress: "203.0.113.11" });
    headDenied.expectStatus(429).expectNoBody();

    const boom = await client.get("/api/boom");
    boom.expectStatus(500).expectProblem({ status: 500, code: "SLOPPY_E_HANDLER_ERROR" });
    assert.equal(boom.text().includes(SECRET_MARKER), false);

    host.diagnostics.expectCode("SLOPPY_TESTHOST_REQUEST").expectNoSecretLeaks();
    assert.equal(
        host.metrics.snapshot().some((metric) => metric.name === "http.requests.total"),
        true,
        "TestHost should emit request metrics",
    );
    assert.equal(
        app.metrics.snapshot().metrics.some((metric) => metric.name === "http.requests.total"),
        true,
        "app metrics should include route-level HTTP metrics",
    );
    assert.equal(JSON.stringify(app.metrics.snapshot()).includes("user-1"), false);
} finally {
    await host.close();
}
