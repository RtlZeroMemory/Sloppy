# Rate limiting

Sloppy ships a first-party rate limiter for HTTP routes and app-host WebSocket
upgrades. Policies are attached per-route through a fluent `.rateLimit(...)`
builder; the runtime enforces them before the handler runs and returns a
`429 Too Many Requests` ProblemDetails response when a request exceeds quota.

This page shows the common shapes. The full option reference lives at
[Core APIs / RateLimit](../api/rate-limit.md), and the matrix-style reference
at [Reference / Rate limit](../reference/rate-limit.md).

## Limit a single route

```ts
import { RateLimit, Results, Sloppy } from "sloppy";

const app = Sloppy.create();

app.post("/login", (ctx) => Results.ok({ ok: true }))
    .rateLimit(RateLimit.slidingWindow({
        name: "login",
        limit: 5,
        windowMs: 60_000,
        partitionBy: RateLimit.partition.ip(),
    }));

export default app;
```

The denied response includes ProblemDetails status `429`, the error code
`SLOPPY_E_RATE_LIMIT_EXCEEDED`, and `Retry-After`, `RateLimit-Limit`,
`RateLimit-Remaining`, and `RateLimit-Reset` headers.

## Algorithms

Pick the policy factory that matches the shape of the limit you want:

- `RateLimit.fixedWindow({ limit, windowMs, partitionBy, ... })` — burst budget
  per fixed wall-clock window. Cheap, slightly bursty at window edges.
- `RateLimit.slidingWindow({ limit, windowMs, partitionBy, ... })` — smoother
  enforcement across the trailing window.
- `RateLimit.tokenBucket({ capacity, refillPerSecond, partitionBy, ... })` —
  steady-state rate with a refillable burst capacity.
- `RateLimit.concurrency({ limit, partitionBy, ... })` — bounds the number of
  in-flight requests for the same partition. The lease is released when the
  handler resolves, throws, or finishes producing a bounded
  `Results.stream(...)` descriptor.

All policies accept `name`, `store`, `cost`, `skip`, `statusCode: 429`, and an
optional `problem` override. `cost` can be a number or a function of the
request context.

## Partition requests

Quota is keyed by a partition selector. Use an explicit helper rather than
constructing keys by hand — the helpers hash sensitive values before they touch
the store, metrics, or diagnostics:

- `RateLimit.partition.ip()` — remote address from the connection or request
- `RateLimit.partition.ip({ trustProxy: true })` — first `X-Forwarded-For`
  entry, only safe behind a proxy that sanitizes the header
- `RateLimit.partition.user()` — authenticated user (requires an auth principal)
- `RateLimit.partition.apiKey()` — resolved API key id
- `RateLimit.partition.header(name)` — arbitrary header
- `RateLimit.partition.claim(name)` — authenticated claim value
- `RateLimit.partition.routeParam(name)` — value of a route parameter
- `RateLimit.partition.custom((ctx) => string)` — last-resort custom selector

Chain `.orIp()` to fall back to the IP when the primary selector is empty (for
example, a user partition on an anonymous endpoint).

Policies without an explicit `name` are scoped to the route method and pattern,
so two routes do not accidentally share quota. Reusing the same `name`
intentionally opts into sharing for matching store, algorithm, selector
metadata, and partition hash.

## Combine user and IP limits

You can attach multiple policies to a route — each runs independently and the
denial reason is reported by `RateLimit-Policy`:

```ts
app.post("/login", login)
    .rateLimit(RateLimit.slidingWindow({
        name: "login-ip",
        limit: 20,
        windowMs: 60_000,
        partitionBy: RateLimit.partition.ip(),
    }))
    .rateLimit(RateLimit.slidingWindow({
        name: "login-user",
        limit: 5,
        windowMs: 60_000,
        partitionBy: RateLimit.partition.user().orIp(),
    }));
```

## Stores

The default store is bounded in-process memory. It supports every algorithm,
TTL cleanup, `maxKeys`, stats, reset, disposal, and health checks.

```ts
const burst = RateLimit.memory({ maxKeys: 10_000 });
app.services.addRateLimitStore("burst", burst);

app.post("/upload", upload)
    .rateLimit(RateLimit.fixedWindow({
        limit: 20,
        windowMs: 60_000,
        store: "burst",
        partitionBy: RateLimit.partition.user().orIp(),
    }));
```

For a distributed store, `RateLimit.redis(...)` exists as a named adapter slot
so apps can declare intent without an accidental memory fallback. In the
current alpha there is no first-party Redis provider on the default `main`
build, so `RateLimit.redis(...)` and `Health.rateLimit(RateLimit.redis(...))`
**fail closed** — they do not silently fall back to memory. See
[Reference / Rate limit](../reference/rate-limit.md) for the exact failure
codes and health reporting.

## Test with TestHost

`TestHost.create(app, { clock })` lets window and refill tests advance time
deterministically with `FakeClock.fixed(...)`, and `host.expectRateLimited(...)`
asserts the `429` ProblemDetails response shape. Inject isolated stores per
test with `rateLimit.stores`.

```ts
import { TestHost, FakeClock } from "sloppy";

const clock = FakeClock.fixed("2026-01-01T00:00:00Z");
const host = TestHost.create(app, { clock });

for (let i = 0; i < 5; i += 1) {
    await host.request("POST", "/login").expectStatus(200);
}
await host.request("POST", "/login").expectRateLimited({ policy: "login" });
```

## WebSockets

`app.websocket(...).rateLimit(policy)` runs on the upgrade attempt before the
socket handler accepts. A denied app-host WebSocket connection rejects with
status `429` and does not invoke the socket handler.

## Diagnostics and privacy

Rate-limit metrics and diagnostics report policy name, route pattern,
algorithm, store kind, partition hash, outcome, reason, and retry delay. They
never include raw IPs, user IDs, API keys, headers, cookies, or `Authorization`
values. See [Reference / Rate limit](../reference/rate-limit.md) for the metric
names.

## Examples

- `examples/rate-limit-basic` — sliding-window IP limit on a login route
- `examples/rate-limit-auth` — user-partitioned limits on authenticated routes
- `examples/rate-limit-redis` — declares the Redis adapter and its fail-closed
  behavior in this build
- `examples/rate-limit-testhost` — deterministic windows under `FakeClock`
- `examples/rate-limit-websocket` — WebSocket upgrade rate limiting

Current support boundaries are tracked in the
[stability matrix](../reference/stability.md).
