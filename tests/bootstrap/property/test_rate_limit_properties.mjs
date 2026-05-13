import assert from "node:assert/strict";

import { RateLimit } from "../../../stdlib/sloppy/rate-limit.js";

const safeHeaders = ["x-api-key", "retry-after", "x_slop_token", "x.trace"];
for (const name of safeHeaders) {
    assert.equal(RateLimit.partition.header(name).metadata.name, name.toLowerCase());
}

const unsafeHeaders = ["bad header", "x\nbad", ""];
for (const name of unsafeHeaders) {
    assert.throws(() => RateLimit.partition.header(name), /HTTP token/);
}

for (let retryAfterMs = 1; retryAfterMs < 5000; retryAfterMs += 137) {
    const store = RateLimit.memory();
    const policy = RateLimit.fixedWindow({
        name: `retry-${retryAfterMs}`,
        limit: 1,
        windowMs: retryAfterMs,
        partitionBy: RateLimit.partition.ip(),
    });
    await store.check({ policy, cost: 1, partitionHash: "a", nowMs: 0 });
    const denied = await store.check({ policy, cost: 1, partitionHash: "a", nowMs: 0 });
    assert.equal(denied.allowed, false);
    assert.ok(denied.retryAfterMs >= 1);
    assert.ok(denied.retryAfterMs <= retryAfterMs);
}
