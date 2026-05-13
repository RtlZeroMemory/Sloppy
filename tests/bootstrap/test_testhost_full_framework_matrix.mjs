import assert from "node:assert/strict";
import { randomUUID } from "node:crypto";
import { brotliCompressSync, brotliDecompressSync, gzipSync, gunzipSync } from "node:zlib";
import { mkdir, mkdtemp, rm, writeFile } from "node:fs/promises";
import { join } from "node:path";
import { tmpdir } from "node:os";

import {
    BackgroundService,
    RateLimit,
    Realtime,
    Results,
    Schema,
    Sloppy,
    TestHost,
    Time,
    WorkQueue,
    schema,
} from "../../stdlib/sloppy/index.js";

async function flush(turns = 5) {
    for (let index = 0; index < turns; index += 1) {
        await Promise.resolve();
    }
}

async function runRouteAndMiddlewareMatrix() {
    const SECRET = "FULL_FRAMEWORK_SECRET_SHOULD_NOT_LEAK";
    const builder = Sloppy.createBuilder();
    builder.config.addObject({ Matrix: { Name: "full-framework" } });
    builder.services.addSingleton("singleton", Object.freeze({ value: "root" }));
    builder.services.addScoped("scope", () => ({ id: randomUUID(), dispose() {} }));
    const app = builder.build();
    const order = [];
    let shortCircuitHandlerCalled = false;

    app.useErrors({ includeDetails: false });
    app.useCors({
        origins: ["https://client.example"],
        methods: ["GET", "POST", "PUT", "PATCH", "DELETE"],
        headers: ["content-type", "x-trace"],
    });
    app.use(async (ctx, next) => {
        order.push(`app:${ctx.request.method}:before`);
        const response = await next();
        order.push(`app:${ctx.request.method}:after`);
        return Results.status(response.status, response.body, {
            headers: { ...response.headers, "X-App-Middleware": "seen" },
        });
    });
    app.use(async (ctx, next) => {
        if (ctx.request.headers.get("x-short-circuit") === "yes") {
            return Results.accepted({ short: true });
        }
        return next();
    });

    const api = app.group("/matrix")
        .requiresAuth()
        .rateLimit(RateLimit.fixedWindow({
            name: "matrix-default",
            limit: 50,
            windowMs: 1000,
            partitionBy: RateLimit.partition.ip(),
        }));
    api.use(async (ctx, next) => {
        order.push(`group:${ctx.request.method}:before`);
        const response = await next();
        order.push(`group:${ctx.request.method}:after`);
        return response;
    });

    const Body = Schema.object({ name: Schema.string().min(2) });
    api.get("/items/{id:int}", (ctx) => Results.json({
        id: Number(ctx.route.id),
        query: ctx.query.q,
        header: ctx.request.headers.get("x-trace"),
        config: ctx.config.require("Matrix:Name"),
        singleton: ctx.services.get("singleton").value,
        user: ctx.requireUser().sub,
        routePattern: ctx.routePattern,
        remoteAddress: ctx.connection.remoteAddress,
    })).requiresScope("items:read").withName("Matrix.GetItem");
    api.post("/items", async (ctx) => Results.created("/matrix/items/1", await ctx.body.validate(Body)))
        .accepts(Body)
        .returns(201, Body);
    api.put("/items/{id:int}", async (ctx) => Results.json({ id: Number(ctx.route.id), body: await ctx.request.json() }));
    api.patch("/items/{id:int}", async (ctx) => Results.json({ id: Number(ctx.route.id), text: await ctx.request.text() }));
    api.delete("/items/{id:int}", () => Results.noContent());
    api.get("/short", () => {
        shortCircuitHandlerCalled = true;
        return Results.ok({ reached: true });
    }).allowAnonymous();
    api.get("/boom", () => {
        throw new Error(SECRET);
    }).requiresScope("items:read");

    const host = await TestHost.create(app, {
        secrets: { matrix: SECRET },
        rateLimit: {
            stores: {
                default: RateLimit.memory({ name: "matrix-default-store", maxKeys: 128 }),
            },
        },
    });
    try {
        const user = host.asUser({ sub: "u1", scopes: ["items:read"], roles: ["reader"] });
        order.length = 0;
        await user.get("/matrix/items/7", { remoteAddress: "198.51.100.7" })
            .query({ q: "recent" })
            .header("x-trace", "trace-1")
            .expectStatus(200)
            .then((response) => response
                .expectHeader("x-app-middleware", "seen")
                .expectJson({
                    id: 7,
                    query: "recent",
                    header: "trace-1",
                    config: "full-framework",
                    singleton: "root",
                    user: "u1",
                    routePattern: "/matrix/items/{id:int}",
                    remoteAddress: "198.51.100.7",
                }));
        assert.deepEqual(order, [
            "app:GET:before",
            "group:GET:before",
            "group:GET:after",
            "app:GET:after",
        ]);

        await user.head("/matrix/items/7")
            .then((response) => response.expectStatus(200).expectNoBody().expectHeader("x-app-middleware", "seen"));
        await user.post("/matrix/items").json({ name: "Ada" })
            .then((response) => response.expectStatus(201).expectJson({ name: "Ada" }));
        await user.post("/matrix/items").json({ name: "A" })
            .then((response) => response.expectStatus(400).expectProblem({ code: "SLOPPY_E_VALIDATION_FAILED" }));
        await user.put("/matrix/items/7").json({ status: "ok" })
            .then((response) => response.expectStatus(200).expectJson({ id: 7, body: { status: "ok" } }));
        await user.patch("/matrix/items/7").text("patch-body")
            .then((response) => response.expectStatus(200).expectJson({ id: 7, text: "patch-body" }));
        await user.delete("/matrix/items/7").expectStatus(204);

        await host.get("/matrix/items/7")
            .then((response) => response.expectStatus(401).expectProblem({ code: "SLOPPY_E_AUTH_UNAUTHORIZED" }));
        await host.asUser({ sub: "u2", scopes: [] }).get("/matrix/items/7")
            .then((response) => response.expectStatus(403).expectProblem({ code: "SLOPPY_E_AUTH_FORBIDDEN" }));

        await host.get("/matrix/short").header("x-short-circuit", "yes")
            .then((response) => response.expectStatus(202).expectJson({ short: true }));
        assert.equal(shortCircuitHandlerCalled, false);

        const preflight = await host.options("/matrix/items/7", {
            headers: {
                origin: "https://client.example",
                "access-control-request-method": "GET",
                "access-control-request-headers": "content-type,x-trace",
            },
        });
        preflight.expectStatus(204)
            .expectHeader("access-control-allow-origin", "https://client.example")
            .expectNoBody();

        const boom = await user.get("/matrix/boom");
        boom.expectStatus(500).expectProblem({ code: "SLOPPY_E_HANDLER_ERROR" });
        assert.equal(boom.text().includes(SECRET), false);
        host.diagnostics.expectNoSecretLeaks();
        host.metrics.expectCounter("http.requests.total", 1, { method: "GET", status: "500" });
    } finally {
        await host.close();
    }
}

async function runStaticAssetMatrix() {
    const previousCwd = process.cwd();
    const root = await mkdtemp(join(tmpdir(), "sloppy-full-static-"));
    try {
        await mkdir(join(root, "public", "nested"), { recursive: true });
        await mkdir(join(root, "dist", "assets"), { recursive: true });
        await writeFile(join(root, "public", "index.html"), "<main>Index</main>");
        await writeFile(join(root, "public", "app.js"), "console.log('asset');\n");
        await writeFile(join(root, "public", "app.js.gz"), gzipSync("console.log('asset');\n"));
        await writeFile(join(root, "public", "app.js.br"), brotliCompressSync(Buffer.from("console.log('asset');\n")));
        await writeFile(join(root, "public", "nested", "data.txt"), "nested-data");
        await writeFile(join(root, "public", ".env"), "secret=1\n");
        await writeFile(join(root, "dist", "index.html"), "<main>SPA</main>");
        await writeFile(join(root, "dist", "assets", "client.js"), "globalThis.ready = true;\n");
        process.chdir(root);

        const app = Sloppy.create();
        app.get("/api/status", () => Results.ok({ ok: true }));
        app.staticFiles("/assets", {
            root: "public",
            precompressed: ["br", "gzip"],
            dotfiles: "deny",
            cacheControl: "public, max-age=60",
        });
        app.spa("/", {
            root: "dist",
            fallback: "index.html",
            precompressed: true,
            cacheControl: {
                assets: "public, max-age=31536000, immutable",
                html: "no-cache",
            },
        });

        const host = await TestHost.create(app);
        try {
            const index = await host.get("/assets/");
            index.expectStatus(200).expectHeader("content-type", /html/u).expectText("<main>Index</main>");
            await host.head("/assets/nested/data.txt")
                .then((response) => response.expectStatus(200).expectNoBody().expectHeader("accept-ranges", "bytes"));
            await host.get("/assets/nested/data.txt", { headers: { range: "bytes=-4" } })
                .then((response) => response.expectStatus(206).expectHeader("content-range", "bytes 7-10/11").expectText("data"));
            await host.get("/assets/nested/data.txt", { headers: { range: "bytes=9-2" } })
                .then((response) => response.expectStatus(416).expectNoBody());
            const asset = await host.get("/assets/app.js");
            await host.get("/assets/app.js", { headers: { "if-modified-since": new Date(Date.now() + 60_000).toUTCString() } })
                .then((response) => response.expectStatus(304).expectNoBody());
            await host.get("/assets/app.js", { headers: { "if-none-match": asset.headers.get("etag") } })
                .then((response) => response.expectStatus(304).expectNoBody());
            await host.get("/assets/app.js", { headers: { "accept-encoding": "gzip;q=0.9, br;q=0.1" } })
                .then((response) => {
                    response.expectStatus(200).expectHeader("content-encoding", "gzip");
                    assert.equal(gunzipSync(response.bytes()).toString("utf8"), "console.log('asset');\n");
                });
            await host.get("/assets/app.js", { headers: { "accept-encoding": "br" } })
                .then((response) => {
                    response.expectStatus(200).expectHeader("content-encoding", "br");
                    assert.equal(brotliDecompressSync(response.bytes()).toString("utf8"), "console.log('asset');\n");
                });
            await host.get("/assets/.env").then((response) => response.expectStatus(403));
            await host.get("/assets/%2e%2e/package.json").then((response) => response.expectStatus(403));
            await host.get("/dashboard/deep/link").then((response) => response.expectStatus(200).expectText("<main>SPA</main>"));
            await host.get("/assets/missing.js").then((response) => response.expectStatus(404));
            await host.get("/api/status").then((response) => response.expectStatus(200).expectJson({ ok: true }));
        } finally {
            await host.close();
        }
    } finally {
        process.chdir(previousCwd);
        await rm(root, { recursive: true, force: true });
    }
}

async function runWebSocketAndRealtimeMatrix() {
    const Chat = Realtime.channel("matrixChat", {
        client: {
            send: Realtime.event(schema.object({ text: schema.string().maxLength(16) })).requiresScope("chat:send"),
            who: schema.object({ room: schema.string() }),
        },
        server: {
            ready: schema.object({ room: schema.string() }),
            message: schema.object({ text: schema.string() }),
            count: schema.object({ value: schema.int() }),
        },
    });
    const app = Sloppy.create();
    app.ws("/matrix/ws", {
        protocols: ["sloppy.matrix"],
        origins: ["https://client.example"],
        maxMessageBytes: 64,
        maxSendQueueBytes: 1024,
    }, async (ctx, socket) => {
        ctx.requireUser();
        await socket.accept();
        await socket.sendText("ready");
        const text = await socket.messages().take(1000, "text");
        assert.equal(text.kind, "text");
        await socket.sendText(`echo:${text.text}`);
        const json = await socket.messages().take(1000, "json");
        assert.equal(json.kind, "json");
        await socket.sendJson({ echoed: json.json() });
        const binary = await socket.messages().take(1000, "binary");
        assert.equal(binary.kind, "binary");
        await socket.sendBytes(binary.bytes);
        const ping = await socket.messages().take(1000, "ping");
        assert.equal(ping.kind, "ping");
        await socket.sendPong(ping.text);
        await socket.messages().take(1000, "close request");
        await socket.close(1000, "done");
    }).requiresAuth().requiresScope("ws");
    app.realtime("/matrix/rooms/{room}", Chat, async (ctx) => {
        await ctx.accept();
        await ctx.groups.join(`room:${ctx.params.room}`);
        await ctx.presence.set({ userId: ctx.user.sub });
        await ctx.send("ready", { room: ctx.params.room });
        ctx.on("send", async (input) => {
            await ctx.group(`room:${ctx.params.room}`).broadcast("message", { text: input.text });
        });
        ctx.on("who", async () => {
            await ctx.send("count", { value: (await ctx.presence.inGroup(`room:${ctx.params.room}`)).length });
        });
    }, { presence: true, validationFailurePolicy: "error" }).requiresAuth();

    const host = await TestHost.create(app);
    try {
        await host.websocket("/matrix/ws")
            .origin("https://evil.example")
            .protocols(["sloppy.matrix"])
            .asUser({ sub: "u1", scopes: ["ws"] })
            .connect()
            .expectRejected(403);
        await host.websocket("/matrix/ws")
            .origin("https://client.example")
            .protocols(["wrong.protocol"])
            .asUser({ sub: "u1", scopes: ["ws"] })
            .connect()
            .expectRejected(400);
        await host.websocket("/matrix/ws")
            .origin("https://client.example")
            .protocols(["sloppy.matrix"])
            .asUser({ sub: "u1", scopes: [] })
            .connect()
            .expectRejected(403);

        const socket = await host.websocket("/matrix/ws")
            .origin("https://client.example")
            .protocols(["sloppy.matrix"])
            .asUser({ sub: "u1", scopes: ["ws"] })
            .connect();
        assert.equal(socket.protocol, "sloppy.matrix");
        await socket.expectText("ready");
        await socket.sendText("hello");
        await socket.expectText("echo:hello");
        await socket.sendJson({ ok: true });
        await socket.expectJson({ echoed: { ok: true } });
        await socket.sendBytes(new Uint8Array([1, 2, 3]));
        await socket.expectBytes(new Uint8Array([1, 2, 3]));
        await socket.sendPing("hi");
        await socket.expectPong();
        await socket.sendText("close");
        await socket.expectClose(1000);

        const alice = await host.realtime("/matrix/rooms/r1", Chat)
            .asUser({ sub: "alice", scopes: ["chat:send"] })
            .connect();
        const bob = await host.realtime("/matrix/rooms/r1", Chat)
            .asUser({ sub: "bob", scopes: [] })
            .connect();
        await alice.expect("ready", { room: "r1" });
        await bob.expect("ready", { room: "r1" });
        await alice.send("send", { text: "hello" });
        await bob.expect("message", { text: "hello" });
        await bob.send("send", { text: "denied" });
        await bob.expectError("SLOPPY_E_REALTIME_UNAUTHORIZED_EVENT");
        await bob.send("who", { room: "r1" });
        await bob.expect("count", { value: 2 });
        await alice.close();
        await bob.close();

        host.metrics.expectCounter("websocket.upgrades.total", 1, { route: "/matrix/ws", outcome: "accepted" });
        host.metrics.expectCounter("websocket.upgrades.rejected.total", 1, { outcome: "origin" });
        host.metrics.expectCounter("websocket.upgrades.rejected.total", 1, { outcome: "protocol" });
        host.metrics.expectCounter("realtime.groups.join.total", 2, { route: "/matrix/rooms/{room}", channel: "matrixChat" });
    } finally {
        await host.close();
    }
}

async function runJobsAndSchedulerMatrix() {
    const clock = Time.fakeClock({ now: new Date("2026-01-01T00:00:00Z") });
    const fired = [];
    const scheduled = Time.every(1000, async (ctx) => {
        fired.push({ run: ctx.run, scheduledAt: ctx.scheduledAt.toISOString() });
    }, { clock, immediate: true, maxRuns: 3 });
    await flush();
    assert.deepEqual(fired.map((entry) => entry.run), [1]);
    clock.advanceBy(1000);
    await flush();
    assert.deepEqual(fired.map((entry) => entry.run), [1, 2]);
    scheduled.pause();
    clock.advanceBy(1000);
    await flush();
    assert.deepEqual(fired.map((entry) => entry.run), [1, 2]);
    scheduled.resume();
    clock.advanceBy(1000);
    await flush();
    assert.deepEqual(fired.map((entry) => entry.run), [1, 2, 3]);
    await scheduled.stop();

    const queue = WorkQueue.create("matrix-mail", { maxQueued: 4, concurrency: 1, retry: { maxAttempts: 2, backoffMs: 0 } });
    let attempts = 0;
    queue.process(async (job) => {
        attempts += 1;
        if (job.data.failOnce && job.attempt === 1) {
            throw new Error("planned");
        }
        return { sent: job.data.id };
    });
    assert.deepEqual(await queue.enqueue({ id: "welcome", failOnce: true }), { sent: "welcome" });
    assert.equal(attempts, 2);

    let backgroundStarted = false;
    const background = BackgroundService.create("matrix-background", async (ctx) => {
        backgroundStarted = true;
        while (!ctx.signal.cancelled) {
            await Promise.resolve();
        }
    });
    const app = Sloppy.create();
    app.use(background);
    app.get("/jobs/enqueue", (ctx) => {
        assert.equal(ctx.lifecycle.startupComplete, true);
        return Results.accepted({ queued: true });
    });
    const jobs = [];
    const host = await TestHost.create(app, {
        jobs: {
            snapshot() {
                return Object.freeze(jobs.map((job) => Object.freeze({ ...job })));
            },
            async runNext() {
                const job = jobs.find((entry) => entry.status === "queued");
                if (job !== undefined) {
                    job.status = "succeeded";
                }
                return job;
            },
        },
    });
    try {
        await flush();
        assert.equal(backgroundStarted, true);
        await host.get("/jobs/enqueue").expectStatus(202);
        host.jobs.enqueue("matrix-mail", { id: "manual" });
        jobs.push({ name: "matrix-mail", payload: { id: "welcome" }, status: "queued" });
        host.jobs.expectEnqueued("matrix-mail", { id: "welcome" });
        await host.jobs.runNext();
        host.jobs.expectSucceeded("matrix-mail");
        assert.equal(app.__getPlanContributions().workers.some((entry) => entry.kind === "backgroundService"), true);
    } finally {
        await host.close();
        await background.stop();
    }
}

await runRouteAndMiddlewareMatrix();
await runStaticAssetMatrix();
await runWebSocketAndRealtimeMatrix();
await runJobsAndSchedulerMatrix();
