# Rate Limit Reference

Rate-limit policies are immutable descriptors consumed by route builders and
the app-host dispatcher.

## Policy Shape

```ts
{
  name?: string;
  partitionBy: RateLimitPartition | ((ctx) => string);
  store?: string | RateLimitStore;
  cost?: number | ((ctx) => number);
  skip?: (ctx) => boolean;
  statusCode?: 429;
  problem?: object | ((ctx, result) => object);
}
```

Algorithm-specific fields:

| Algorithm | Required fields |
| --- | --- |
| `fixedWindow` | `limit`, `windowMs` |
| `slidingWindow` | `limit`, `windowMs` |
| `tokenBucket` | `capacity`, `refillPerSecond` |
| `concurrency` | `limit` |

Numeric values must be positive and bounded. Header partition names must be
HTTP tokens.

## Memory Store

`RateLimit.memory({ name?, maxKeys?, cleanupIntervalMs?, rejectOnMaxKeys? })`
stores hashed partition keys in process memory. Expired entries are cleaned
periodically and before capacity decisions. When full, the store either evicts
the oldest key or rejects with `SLOPPY_E_RATE_LIMIT_STORE_FULL`.

`store.stats()` reports kind, key count, configured key limit, evictions,
rejected keys, and disposal state without raw partition values.

## Redis Store

`RateLimit.redis(redis, { name?, prefix? })` exists so apps can name a
distributed store without accidental memory fallback. In the current `main`
build there is no first-party Redis provider, so `check()` raises
`SLOPPY_E_RATE_LIMIT_REDIS_UNAVAILABLE`.

## Metrics

Route enforcement emits:

- `rate_limit.requests.total`
- `rate_limit.allowed.total`
- `rate_limit.denied.total`
- `rate_limit.tokens.remaining`
- `rate_limit.concurrency.active` for concurrency policies

Labels are `policy`, route pattern, algorithm, store kind, and outcome. They do
not include partition values.

## Diagnostics

Diagnostics include policy name, route pattern, algorithm, store kind,
partition hash, reason, and retry delay. They do not include raw IPs, headers,
tokens, cookies, user IDs, API keys, or Authorization values.
