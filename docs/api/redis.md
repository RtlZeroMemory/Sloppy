# Redis

`Redis` is Sloppy's first-party Redis client. It speaks RESP2 over the Sloppy
outbound TCP bridge and does not use an npm Redis package.

```ts
import { Redis } from "sloppy";

await using redis = Redis.client("main", {
    url: "redis://127.0.0.1:6379/0",
    pool: { maxConnections: 8 },
    commandTimeoutMs: 1000,
});

await redis.set("users:1", { id: 1, name: "Ada" }, { ttlMs: 60_000 });
const user = await redis.get("users:1");
```

The client is experimental. API shape, diagnostics, and Plan metadata can still
change before Sloppy reaches a stable release.

## Runtime Requirements

Redis calls require:

- an active `globalThis.__sloppy.net.connect` bridge;
- a reachable Redis server;
- `globalThis.__sloppy.net.connectTls` when the URL uses `rediss://`.

If the bridge is missing, client operations fail. Sloppy does not fall back to
memory storage and does not start Redis implicitly outside `TestServices.redis`.

## Client Options

```ts
const redis = Redis.client("main", {
    url: "redis://:password@redis.local:6379/0",
    password: Redis.Secret.fromUtf8("password"),
    database: 0,
    connectTimeoutMs: 1000,
    commandTimeoutMs: 1000,
    maxValueBytes: 1024 * 1024,
    pingOnConnect: true,
    pool: {
        maxConnections: 8,
        idleTimeoutMs: 30000,
        pendingQueueLimit: 128,
        acquireTimeoutMs: 1000,
    },
});
```

| Option | Default | Notes |
| --- | --- | --- |
| `url` | required | `redis://` or `rediss://`. Userinfo and password values are redacted from diagnostics. |
| `password` | URL password | String or `Redis.Secret`. Sends `AUTH` when present. |
| `database` | URL path or `0` | Sends `SELECT` after connect when non-zero. |
| `connectTimeoutMs` | `1000` | Bounds TCP/TLS connect. |
| `commandTimeoutMs` | `5000` | Bounds one command or pipeline. |
| `maxValueBytes` | `1048576` | Applies to encoded values and parser bulk limits. |
| `pingOnConnect` | `true` | Sends `PING` during connection setup. |
| `pool.maxConnections` | `4` | Bounded connection pool. |
| `pool.idleTimeoutMs` | `30000` | Idle pooled connection pruning. |
| `pool.pendingQueueLimit` | `64` | Fails when all connections are busy and the wait queue is full. |
| `pool.acquireTimeoutMs` | `1000` | Bounds pool wait time. |

## Values

`set` stores JSON values with a Sloppy value prefix. `get` decodes that value
and optionally validates it with a schema.

```ts
import { Redis, schema } from "sloppy";

const userSchema = schema.object({
    id: schema.number(),
    email: schema.string().email(),
});

await redis.set("user:1", { id: 1, email: "ada@example.test" });
const user = await redis.get("user:1", userSchema);
```

Use `setText`/`getText` for text and `setBytes`/`getBytes` for bytes.

## Commands

The client exposes common Redis commands and a safe escape hatch:

```ts
await redis.ping();
await redis.get("key");
await redis.set("key", { ok: true }, { ttlMs: 1000, nx: true });
await redis.delete("key");
await redis.exists("key");
await redis.mget(["a", "b"]);
await redis.mset({ a: 1, b: 2 }, { ttlMs: 1000 });
await redis.incr("counter");
await redis.decr("counter");
await redis.expire("key", 5000);
await redis.ttl("key");
await redis.pttl("key");
await redis.scan({ match: "prefix:*", count: 100 });
await redis.command("HSET", ["users:1", "name", "Ada"]);
```

`command(name, args)` validates the command token and sends the argument array as
RESP bulk strings. It does not expose raw sockets or Redis server handles.

## Pipelines And Scripts

```ts
const replies = await redis.pipeline([
    ["SET", "a", "1"],
    ["GET", "a"],
]);

const deleted = await redis.script(
    "return redis.call('del', KEYS[1])",
    ["a"],
);
```

`script(...)` loads the Lua script, calls `EVALSHA`, and retries once after a
`NOSCRIPT` reply. Lua script text is bounded to 256 KiB.

## Locks

`Redis.locks(client)` provides single-key leases backed by `SET key owner PX ttl
NX` plus Lua release/extend checks.

```ts
const locks = Redis.locks(redis, { prefix: "myapp:locks:" });
await using lease = await locks.acquire("daily-report", {
    ttlMs: 30000,
    waitTimeoutMs: 5000,
});

await runReport();
await lease.extend(30000);
```

Releasing or extending a lease checks the owner token. The token is never
returned from `lease.owner`; diagnostics redact it.

## Metrics, Diagnostics, And Health

```ts
redis.metrics();
redis.diagnostics();
await redis.health();
```

Redis client metrics use low-cardinality labels: client name, command, and
outcome. Raw keys, URLs, passwords, and values are not metric labels.

`diagnostics()` returns redacted endpoint and pool state. `health()` runs a
bounded `PING` and returns `healthy` or `unhealthy`.

## Limits

- RESP3, cluster, sentinel, pub/sub, streams, modules, client-side caching, and
  Redis functions are not claimed by this API.
- `rediss://` requires the Sloppy TLS bridge.
- Client operations require the Sloppy network bridge; default Node execution
  without that bridge is not a Redis runtime lane.
- Command errors are surfaced as `SloppyRedisError` with the Redis error code
  kept in details.
