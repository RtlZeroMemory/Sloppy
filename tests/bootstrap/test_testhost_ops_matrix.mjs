import assert from "node:assert/strict";

import {
    Health,
    Results,
    Schema,
    Sloppy,
    TestHost,
} from "../../stdlib/sloppy/index.js";

const SECRET_MARKER = "TESTHOST_OPS_MATRIX_SECRET_SHOULD_NOT_LEAK";

const builder = Sloppy.createBuilder();
builder.config.addObject({
    App: {
        Name: "ops-matrix",
    },
    Secrets: {
        ApiKey: SECRET_MARKER,
    },
});

const app = builder.build();
app.useErrors({ includeDetails: false, maxBodyBytes: 128 });

const jobResource = Object.freeze({
    state() {
        return {
            queued: 1,
            failed: 0,
            apiKey: SECRET_MARKER,
            nested: {
                token: SECRET_MARKER,
            },
        };
    },
});

app.health()
    .check("self", Health.self(), { tags: ["live", "ready"], critical: true })
    .check("config", Health.config(["App:Name"]), { tags: ["ready"], critical: true })
    .check("jobs", Health.jobs(jobResource), { tags: ["ready"], critical: false })
    .expose();

app.get("/items/{id:int}", () => Results.ok({ ok: true }))
    .returns(200, Schema.object({ ok: Schema.boolean() }));
app.get("/explode", () => {
    throw new Error(SECRET_MARKER);
});
app.docs({ title: "Ops Matrix", path: "/docs", openapiPath: "/openapi.json" });
app.management({
    path: "/_ops",
    protect(ctx) {
        return ctx.user?.hasRole("ops") === true;
    },
});

const host = await TestHost.create(app, {
    secrets: {
        marker: SECRET_MARKER,
    },
});

try {
    await host.health.expectStatus("healthy");
    await host.health.expect("self", "healthy");
    await host.health.expect("config", "healthy");
    await host.health.expect("jobs", "healthy");
    const health = await host.health.snapshot();
    assert.equal(JSON.stringify(health).includes(SECRET_MARKER), false);
    assert.equal(
        Object.values(health.checks).some((check) => check.name === "jobs" && check.data.apiKey === "[redacted]"),
        true,
    );
    await host.get("/live").then((response) => response.expectStatus(200));
    await host.get("/ready").then((response) => response.expectStatus(200));

    await host.get("/_ops/info").then((response) => response.expectStatus(403));
    const ops = host.asUser({ sub: "operator-1", roles: ["ops"] });
    const runtime = await ops.get("/_ops/runtime");
    runtime.expectStatus(200);
    assert.equal(runtime.json().security.protected, true);
    assert.equal(JSON.stringify(runtime.json()).includes(SECRET_MARKER), false);

    const metrics = await ops.get("/_ops/metrics");
    metrics.expectStatus(200);
    assert.match(metrics.text(), /routing_route_table_size/u);
    assert.equal(metrics.text().includes("operator-1"), false);
    assert.equal(metrics.text().includes(SECRET_MARKER), false);

    const openapi = await host.get("/openapi.json");
    openapi.expectStatus(200);
    assert.equal(openapi.json().paths["/items/{id}"].get.responses["200"].description, "response");
    assert.equal(openapi.text().includes("/docs"), false);
    await host.get("/docs")
        .then((response) => response.expectStatus(200).expectHeader("content-type", /text\/html/u));

    host.jobs.enqueue("send-welcome-email", { email: "ada@example.com" });
    host.jobs.expectEnqueued("send-welcome-email", { email: "ada@example.com" });
    await host.jobs.runNext();
    host.jobs.expectSucceeded("send-welcome-email");

    const boom = await host.get("/explode");
    boom.expectStatus(500).expectProblem({ status: 500, code: "SLOPPY_E_HANDLER_ERROR" });
    assert.equal(boom.text().includes(SECRET_MARKER), false);
    host.diagnostics.expectNoSecretLeaks();
} finally {
    await host.close();
}
