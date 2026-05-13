# Redis Cache

This is an API-shape example for `Cache.redis(...)`.

It shows a Redis-backed cache, tags, `getOrCreate`, health, stats, and app-host
service registration. It does not provide an in-memory fallback when Redis is
unavailable.

Current limitations:

- requires Redis and the Sloppy outbound network bridge when executed;
- tag invalidation is Redis-set based, not pub/sub based;
- `getOrCreate` coalesces same-process misses only;
- cluster, sentinel, and Redis streams are outside this cache contract.
