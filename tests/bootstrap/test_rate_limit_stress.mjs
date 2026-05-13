import assert from "node:assert/strict";

import { RateLimit } from "../../stdlib/sloppy/rate-limit.js";

const store = RateLimit.memory({ maxKeys: 128 });
const policy = RateLimit.fixedWindow({
    name: "stress",
    limit: 1000,
    windowMs: 60_000,
    partitionBy: RateLimit.partition.custom((ctx) => ctx.partition),
});
const startedAt = Date.now();

await Promise.all(Array.from({ length: 64 }, (_, index) => store.check({
    policy,
    cost: 1,
    partitionHash: `p${index % 4}`,
    nowMs: startedAt,
})));

assert.equal(store.stats().keys, 4);

const tokenPolicy = RateLimit.tokenBucket({
    name: "stress-bucket",
    capacity: 10,
    refillPerSecond: 10,
    partitionBy: RateLimit.partition.custom((ctx) => ctx.partition),
});
const first = await store.check({ policy: tokenPolicy, cost: 10, partitionHash: "same", nowMs: startedAt });
const second = await store.check({ policy: tokenPolicy, cost: 1, partitionHash: "same", nowMs: startedAt });
const third = await store.check({ policy: tokenPolicy, cost: 1, partitionHash: "same", nowMs: startedAt + 100 });

assert.equal(first.allowed, true);
assert.equal(second.allowed, false);
assert.equal(third.allowed, true);
