import assert from "node:assert/strict";

import { RateLimit } from "../../stdlib/sloppy/rate-limit.js";

const store = RateLimit.redis(undefined, { prefix: "sloppy:test:rl:" });
assert.equal(store.kind, "redis");
assert.equal(store.name, "redis");

await assert.rejects(
    () => store.check({}),
    /SLOPPY_E_RATE_LIMIT_REDIS_UNAVAILABLE/,
);

const health = await store.health();
assert.equal(health.status, "degraded");
assert.equal(health.errorCode, "SLOPPY_E_RATE_LIMIT_REDIS_UNAVAILABLE");
assert.equal(JSON.stringify(store.stats()).includes("sloppy:test:rl:"), false);
