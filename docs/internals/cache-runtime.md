# Cache Runtime

Sloppy cache lives in the JavaScript app-host stdlib. It integrates with data providers,
route metadata, services, health, metrics, and compiler Plan extraction without adding an
external cache package.

## Boundaries

`stdlib/sloppy/cache.js` owns cache instance behavior. Provider-backed caches depend on
connections created by `stdlib/sloppy/data.js`; they do not reach around the public provider
API or silently substitute memory storage.

`stdlib/sloppy/internal/routes.js` owns output cache policy. The route wrapper runs after
authorization and before response cache header application. This keeps authenticated output
cache partitioning in the same place as route auth metadata.

`stdlib/sloppy/internal/services.js` owns DI registration. `app.services.addCache(cache)`
registers a singleton under `Cache.token(cache.name)`. `app.js` attaches the app metrics
registry when cache services are resolved.

## Data Model

Memory cache stores serialized JSON text in a bounded `Map`. Reads parse JSON and writes
serialize JSON so callers do not share mutable object references with stored entries.
Expiry is checked on read and cleanup. Eviction removes expired entries first, then the
least-recently-used entry.

Distributed cache stores one row per `(namespace, cache_key)`. Tags are stored as JSON and
scanned within a namespace for invalidation. This keeps the provider contract small and
portable across SQLite, PostgreSQL, and SQL Server.

Hybrid cache composes cache instances. It does not own provider details; it delegates
reads, writes, invalidation, cleanup, and disposal to the memory and distributed layers.

## Output Cache Safety

Output cache keys are built from bounded, selected metadata:

- HTTP method
- route pattern
- configured query keys
- configured request headers
- configured route params
- hashed user/claim partitions

The raw request URL, `Authorization`, `Cookie`, and user ID are not used directly. Stored
responses also bypass when the result has `Set-Cookie`, a non-cacheable status, a stream
body, an unsupported descriptor shape, or a body over the configured limit.

Authenticated routes require a user partition by default. A route with auth metadata and no
`varyByUser` bypasses rather than storing a shared response. Shared authenticated caching by
role or claim requires an explicit `allowSharedAuthenticated` option so the application
author has to acknowledge that every user in that partition may receive the same body.

## Compiler And Tooling

`sloppy/cache` imports mark `stdlib.cache` in `requiredFeatures`. Static route chains that
include `.outputCache(...)` and `.cacheHeaders(...)` remain extractable, and static
JSON-compatible options are copied into route Plan metadata.

Dynamic option fields are represented as partial static metadata rather than blocking route
extraction. Runtime validation remains authoritative.
