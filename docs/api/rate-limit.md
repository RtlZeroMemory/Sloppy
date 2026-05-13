# RateLimit

`RateLimit` provides first-party abuse protection for HTTP routes and app-host
WebSocket upgrades.

```ts
import { RateLimit, Results, Sloppy } from "sloppy";

const app = Sloppy.create();

app.post("/login", login)
  .rateLimit(RateLimit.slidingWindow({
    name: "login",
    limit: 5,
    windowMs: 60_000,
    partitionBy: RateLimit.partition.ip(),
  }));

app.get("/me", getMe)
  .requiresAuth()
  .rateLimit(RateLimit.tokenBucket({
    name: "me",
    capacity: 100,
    refillPerSecond: 10,
    partitionBy: RateLimit.partition.user(),
  }));
```

Denied requests return ProblemDetails status `429` with
`SLOPPY_E_RATE_LIMIT_EXCEEDED`, `Retry-After`, `RateLimit-Limit`,
`RateLimit-Remaining`, and `RateLimit-Reset`.

## Algorithms

- `RateLimit.fixedWindow({ limit, windowMs, partitionBy, ... })`
- `RateLimit.slidingWindow({ limit, windowMs, partitionBy, ... })`
- `RateLimit.tokenBucket({ capacity, refillPerSecond, partitionBy, ... })`
- `RateLimit.concurrency({ limit, partitionBy, ... })`

Options also accept `name`, `store`, `cost`, `skip`, `statusCode: 429`, and
`problem`. Costs can be numeric or a function of the request context.

## Partitioning

Use explicit partition helpers:

- `RateLimit.partition.ip()`
- `RateLimit.partition.ip({ trustProxy: true })`
- `RateLimit.partition.user()`
- `RateLimit.partition.apiKey()`
- `RateLimit.partition.header(name)`
- `RateLimit.partition.claim(name)`
- `RateLimit.partition.routeParam(name)`
- `RateLimit.partition.custom(fn)`

`.orIp()` falls back to the request IP when the primary partition is empty.
User and claim partitions require an authenticated request. Raw IPs, user IDs,
API keys, headers, cookies, and tokens are hashed before store keys,
diagnostics, or metrics see them.

`RateLimit.partition.ip()` uses the connection/request remote address and
ignores `X-Forwarded-For` by default. Only use
`RateLimit.partition.ip({ trustProxy: true })` behind a trusted proxy that
sanitizes `X-Forwarded-For`; otherwise spoofed headers must not affect quota
selection.

Policies without an explicit `name` are scoped to the route method and pattern,
so two unnamed routes do not accidentally share quota. Explicitly reusing the
same `name` opts into sharing for the same store, algorithm, selector metadata,
and partition hash.

## Stores

The default store is bounded in-process memory. It supports all algorithms,
TTL cleanup, `maxKeys`, stats, reset, disposal, and health checks.

```ts
const store = RateLimit.memory({ maxKeys: 10_000 });
app.services.addRateLimitStore("burst", store);

app.get("/upload", upload)
  .rateLimit(RateLimit.fixedWindow({
    limit: 20,
    windowMs: 60_000,
    store: "burst",
    partitionBy: RateLimit.partition.user().orIp(),
  }));
```

`RateLimit.redis(...)` is an adapter slot that fails closed in this build
because a first-party Redis provider is not present on `main`. It does not
fall back to memory.

`Health.rateLimit(RateLimit.redis(...))` reports `degraded` while the provider
is absent, even if a supplied object can respond to `ping()`, because
enforcement still fails closed.

## Testing

`TestHost.create(app, { clock })` lets window and refill tests use
`FakeClock.fixed(...)`. `host.expectRateLimited(...)` asserts the default `429`
ProblemDetails response. `rateLimit.stores` can inject isolated stores for a
test host.

Concurrency policies hold a lease for the route handler lifetime. The lease is
released when a synchronous handler returns, an async handler settles, or a
handler throws. Bounded `Results.stream(...)` handlers release after the stream
descriptor has been produced, not after an external client drain.

## WebSockets

`app.websocket(...).rateLimit(policy)` applies to upgrade attempts before the
socket handler accepts. A denied app-host WebSocket connection rejects with
status `429` and does not call the socket handler.
