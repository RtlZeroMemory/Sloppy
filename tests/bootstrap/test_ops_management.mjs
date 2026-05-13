import assert from "node:assert/strict";

import { Health, Metrics, Results, Sloppy, Testing } from "../../stdlib/sloppy/index.js";

function names(routes) {
    return routes.map((route) => `${route.method} ${route.pattern}`);
}

{
    const registry = Metrics.createRegistry();
    registry.counter("orders_created_total").inc({ route: "/orders/{id}" }, 2);
    registry.gauge("queue_depth").set(42).dec(2);
    registry.histogram("db_query_ms", { buckets: [1, 10] }).observe({ provider: "sqlite" }, 7);

    const snapshot = registry.snapshot();
    assert.equal(snapshot.metrics.length, 3);
    assert.equal(snapshot.metrics[0].name, "db_query_ms");
    assert.equal(snapshot.metrics[0].series[0].buckets[0].count, 0);
    assert.equal(snapshot.metrics[0].series[0].buckets[1].count, 1);
    assert.match(registry.renderPrometheus(), /db_query_ms_bucket\{provider="sqlite",le="10"\} 1/);
    assert.match(registry.renderPrometheus(), /orders_created_total\{route="\/orders\/\{id\}"\} 2/);
}

{
    const registry = Metrics.createRegistry();
    const counter = registry.counter("bounded_total", { maxLabelSets: 1 });
    counter.inc({ route: "/a" });
    counter.inc({ route: "/b" });
    assert.equal(registry.snapshot().cardinalityDrops, 1);
    assert.equal(registry.snapshot().metrics[0].series.length, 1);
}

{
    const health = Health.createRegistry();
    let calls = 0;
    health
        .check("self", Health.self(), { tags: ["live", "ready", "startup"], cacheMs: 1000 })
        .check("db", () => ({
            status: "unhealthy",
            message: "db failed",
            data: {
                connectionString: "server=prod;password=secret",
                safe: "visible",
            },
        }), { tags: ["ready"], critical: false })
        .check("cached", () => {
            calls += 1;
            return true;
        }, { tags: ["ready"], cacheMs: 1000 });

    const ready = await health.evaluate("ready");
    assert.equal(ready.status, "degraded");
    assert.equal(ready.checks.db.data.connectionString, "[redacted]");
    assert.equal(ready.checks.db.data.safe, "visible");
    assert.equal(ready.summary.healthy, 2);
    assert.equal(ready.summary.unhealthy, 1);

    await health.evaluate("ready");
    assert.equal(calls, 1);
}

{
    const health = Health.createRegistry();
    health.check("slow", () => new Promise(() => {}), { tags: ["ready"], timeoutMs: 1 });
    const ready = await health.evaluate("ready");
    assert.equal(ready.status, "unhealthy");
    assert.equal(ready.checks.slow.errorCode, "SLOPPY_E_HEALTH_TIMEOUT");
}

{
    const health = Health.createRegistry();
    health
        .check("slow-a", () => new Promise(() => {}), { tags: ["ready"], timeoutMs: 40 })
        .check("slow-b", () => new Promise(() => {}), { tags: ["ready"], timeoutMs: 40 })
        .check("slow-c", () => new Promise(() => {}), { tags: ["ready"], timeoutMs: 40 });
    const started = Date.now();
    const ready = await health.evaluate("ready");
    const elapsedMs = Date.now() - started;
    assert.equal(ready.status, "unhealthy");
    assert.equal(Object.keys(ready.checks).join(","), "slow-a,slow-b,slow-c");
    assert.equal(elapsedMs < 110, true);
}

{
    const app = Sloppy.create();
    app.management();
    const registry = app.__getHealthRegistry();

    const starting = await registry.evaluate("startup", {
        lifecycle: { startupComplete: false, shuttingDown: false },
    });
    assert.equal(starting.status, "unhealthy");
    assert.equal(starting.checks.runtime.errorCode, "SLOPPY_E_RUNTIME_STARTING");

    registry.resetCache();
    const draining = await registry.evaluate("ready", {
        lifecycle: { startupComplete: true, shuttingDown: true },
    });
    assert.equal(draining.status, "unhealthy");
    assert.equal(draining.checks.runtime.errorCode, "SLOPPY_E_RUNTIME_SHUTTING_DOWN");

    registry.resetCache();
    const liveDuringDrain = await registry.evaluate("live", {
        lifecycle: { startupComplete: true, shuttingDown: true },
    });
    assert.equal(liveDuringDrain.status, "healthy");
}

{
    const app = Sloppy.create();
    app.health()
        .check("self", Health.self(), { tags: ["live", "ready", "startup"], critical: true })
        .check("optional", () => ({ status: "degraded", message: "optional dependency slow" }), {
            tags: ["health"],
            critical: false,
        })
        .expose();

    assert.deepEqual(names(app.__getRoutes()), [
        "GET /health",
        "GET /live",
        "GET /ready",
        "GET /startup",
    ]);

    const host = Testing.createHost(app);
    assert.equal((await host.get("/live")).status, 200);
    assert.equal((await host.get("/ready")).status, 200);
    const detailed = await (await host.get("/health")).json();
    assert.equal(detailed.status, "degraded");
    assert.equal(detailed.checks.optional.status, "degraded");
    await host.close();
}

{
    const exampleApp = Sloppy.create();
    const ordersCreated = exampleApp.metrics.counter("orders_created_total", {
        description: "Orders accepted by the example app.",
    });
    exampleApp.health()
        .check("self", Health.self(), { tags: ["live", "ready", "startup"] })
        .check("memory", Health.memory(), { tags: ["health"], critical: false, cacheMs: 1000 })
        .expose();
    exampleApp.post("/orders", () => {
        ordersCreated.inc({ route: "/orders" });
        return Results.accepted({ accepted: true });
    });
    exampleApp.management({
        path: "/_sloppy",
        protect: (ctx) => ctx.request.headers.get("x-ops-key") === "local-dev-key",
    });

    const host = Testing.createHost(exampleApp);
    assert.equal((await host.get("/live")).status, 200);
    assert.equal((await host.post("/orders")).status, 202);
    assert.equal((await host.get("/_sloppy/metrics")).status, 403);
    const metrics = await (await host.get("/_sloppy/metrics", {
        headers: { "x-ops-key": "local-dev-key" },
    })).text();
    assert.match(metrics, /orders_created_total\{route="\/orders"\} 1/);
    await host.close();
}

{
    const app = Sloppy.create();
    assert.equal(app.__getRoutes().length, 0);
    app.get("/orders/{id}", () => Results.ok({ ok: true }));
    app.management({
        protect: (ctx) => ctx.request.headers.get("x-ops") === "yes",
    });

    assert.deepEqual(names(app.__getRoutes()).filter((route) => route.includes("/_sloppy")), [
        "GET /_sloppy/health",
        "GET /_sloppy/live",
        "GET /_sloppy/ready",
        "GET /_sloppy/startup",
        "GET /_sloppy/metrics",
        "GET /_sloppy/metrics.json",
        "GET /_sloppy/info",
        "GET /_sloppy/runtime",
    ]);

    const host = Testing.createHost(app);
    assert.equal((await host.get("/_sloppy/info")).status, 403);
    assert.equal((await host.get("/orders/42")).status, 200);
    const metricsText = await (await host.get("/_sloppy/metrics", {
        headers: { "x-ops": "yes" },
    })).text();
    assert.match(metricsText, /http_requests_total\{method="GET",route="\/orders\/\{id\}"\} 1/);
    assert.match(metricsText, /http_route_hits\{method="GET",route="\/orders\/\{id\}"\} 1/);
    assert.match(metricsText, /http_response_bytes\{method="GET",route="\/orders\/\{id\}"\} [1-9][0-9]*/);
    assert.match(metricsText, /runtime_uptime_seconds [0-9]/);
    assert.match(metricsText, /routing_route_table_size [0-9]/);
    assert.equal(metricsText.includes("/orders/42"), false);

    const metricsJson = await (await host.get("/_sloppy/metrics.json", {
        headers: { "x-ops": "yes" },
    })).json();
    assert.equal(metricsJson.metrics.some((metric) => metric.name === "http.requests.total"), true);
    assert.equal(metricsJson.metrics.some((metric) => metric.name === "runtime.uptime.seconds"), true);

    const info = await (await host.get("/_sloppy/info", {
        headers: { "x-ops": "yes" },
    })).json();
    assert.equal(info.security.protected, true);

    const runtime = await (await host.get("/_sloppy/runtime", {
        headers: { "x-ops": "yes" },
    })).json();
    assert.equal(runtime.routes.management, 8);
    assert.equal(runtime.lifecycle.startupComplete, true);
    assert.equal(JSON.stringify(runtime).includes("secret"), false);
    assert.equal(app.__getPlanContributions().ops.managementExposed, true);
    await host.close();
    assert.equal(app.__getLifecycle().shuttingDown, true);
}

{
    const app = Sloppy.create();
    app.management({ path: "/" });

    assert.deepEqual(names(app.__getRoutes()), [
        "GET /health",
        "GET /live",
        "GET /ready",
        "GET /startup",
        "GET /metrics",
        "GET /metrics.json",
        "GET /info",
        "GET /runtime",
    ]);
}

{
    const builder = Sloppy.createBuilder();
    let disposed = 0;
    builder.services.addScoped("request.cleanup", () => ({
        dispose() {
            disposed += 1;
        },
    }));
    const app = builder.build();
    app.get("/cleanup", (ctx) => {
        ctx.services.get("request.cleanup");
        return Results.ok({ ok: true });
    });
    app.__getMetricsRegistry().counter("http.requests.active");

    const host = Testing.createHost(app);
    assert.equal((await host.get("/cleanup")).status, 200);
    assert.equal(disposed, 1);
    await host.close();
}
