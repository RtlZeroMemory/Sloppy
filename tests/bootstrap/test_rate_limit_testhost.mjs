import assert from "node:assert/strict";

import { FakeClock, RateLimit, Results, Sloppy, TestHost } from "../../stdlib/sloppy/index.js";

const clock = FakeClock.fixed("2026-01-01T00:00:00Z");
const app = Sloppy.create();
app.get("/clocked", () => Results.ok({ ok: true })).rateLimit(RateLimit.fixedWindow({
    name: "clocked",
    limit: 1,
    windowMs: 1000,
    partitionBy: RateLimit.partition.ip(),
}));
app.post("/json-clocked", () => Results.ok({ ok: true })).rateLimit(RateLimit.fixedWindow({
    name: "json-clocked",
    limit: 1,
    windowMs: 1000,
    partitionBy: RateLimit.partition.ip(),
}));

const host = await TestHost.create(app, {
    clock,
    rateLimit: {
        stores: {
            default: RateLimit.memory({ name: "test-default", maxKeys: 16 }),
            isolated: RateLimit.memory({ name: "isolated", maxKeys: 16 }),
        },
    },
});

await host.get("/clocked", { remoteAddress: "192.0.2.1" }).then((response) => response.expectStatus(200));
await host.expectRateLimited("GET", "/clocked", { remoteAddress: "192.0.2.1" });
host.advanceClock({ milliseconds: 1001 });
await host.get("/clocked", { remoteAddress: "192.0.2.1" }).then((response) => response.expectStatus(200));
await host.post("/json-clocked", { remoteAddress: "192.0.2.10" }).json({ ok: true }).expectStatus(200);
await host.post("/json-clocked", { remoteAddress: "192.0.2.11" }).json({ ok: true }).expectStatus(200);
await host.expectRateLimited("POST", "/json-clocked", {
    remoteAddress: "192.0.2.10",
    json: { ok: true },
});

const metrics = app.metrics.snapshot();
assert.equal(
    metrics.metrics.some((metric) => metric.name === "rate_limit.denied.total"),
    true,
    "rate-limit denied metric should be emitted",
);
assert.equal(
    JSON.stringify(metrics).includes("192.0.2.1"),
    false,
    "rate-limit metrics must not expose raw IP partitions",
);
