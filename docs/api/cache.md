# Cache

Sloppy ships first-party caches for normal backend app work:

- `Cache.memory(...)` for bounded per-process storage.
- `Cache.sqlite(...)`, `Cache.postgres(...)`, and `Cache.sqlServer(...)` for provider-backed distributed storage.
- `Cache.hybrid(...)` for memory-fronted distributed cache access.
- route-level `.outputCache(...)` for server-side response caching.
- `.cacheHeaders(...)` and `Results.*(...).cacheControl(...)` for client/proxy cache headers.

```ts
import { Cache, Results, Schema, Sloppy } from "sloppy";

const UserDto = Schema.object({
    id: Schema.integer(),
    name: Schema.string(),
});

const app = Sloppy.create();
const cache = Cache.memory("main", { maxEntries: 10000, ttlMs: 10000 });

app.services.addCache(cache);

app.get("/users/{id}", async (ctx) => {
    const user = await cache.getOrCreate(`users:${ctx.route.id}`, {
        ttlMs: 30000,
        tags: [`user:${ctx.route.id}`, "users"],
        schema: UserDto,
    }, async () => {
        return await ctx.services.get("users").findById(ctx.route.id);
    });

    return user === null ? Results.notFound() : Results.json(user);
});
```

## Memory Cache

`Cache.memory(nameOrOptions?, options?)` stores JSON-serializable values in the current process.
It is bounded by `maxEntries`, defaults to a finite size, evicts expired entries first,
then evicts least-recently-used entries.

```ts
const cache = Cache.memory("main", {
    maxEntries: 10000,
    ttlMs: 60000,
});

await cache.set("users:42", { id: 42 }, {
    tags: ["users", "user:42"],
    slidingExpirationMs: 10000,
});
```

Memory cache values are serialized and parsed on write/read so callers do not receive
the original mutable object reference.

## Distributed Cache

Provider-backed caches require a real `sloppy/data` connection. They do not fall back to
memory when a provider is unavailable.

```ts
import { Cache, data } from "sloppy";

const db = data.sqlite.open({
    database: ":memory:",
    capability: "data.main",
});

const cache = Cache.sqlite(db, {
    name: "main",
    namespace: "app",
    table: "sloppy_cache_entries",
    ttlMs: 60000,
});
```

SQLite, PostgreSQL, and SQL Server use provider-specific SQL and store entries by
`namespace` and `cache_key`. `clear()` deletes only the cache namespace. Use
`clear({ dangerouslyClearAll: true })` only when the whole cache table is intentionally
owned by that operation.

## Hybrid Cache

Hybrid cache checks memory first, falls back to the distributed cache, and populates
memory on distributed hits.

```ts
const cache = Cache.hybrid("main", {
    memory: Cache.memory({ maxEntries: 10000, ttlMs: 10000 }),
    distributed: Cache.postgres(db, { ttlMs: 60000 }),
});
```

`set`, `remove`, `invalidateTag`, `invalidateTags`, `clear`, and `cleanup` apply to both
layers. Distributed read failures throw by default; set `failOpenOnDistributedRead: true`
only when a memory miss may safely become a cache miss.

## Cache-Aside

`getOrCreate(key, options, factory)` prevents local stampedes by coalescing concurrent
calls for the same key and namespace.

```ts
const value = await cache.getOrCreate("dashboard:u_1", {
    ttlMs: 10000,
    tags: ["dashboard", "user:u_1"],
}, async (signal) => {
    return await loadDashboard(signal);
});
```

Factory failures are not cached. Schema failures are not cached. `null` is cached by
default; pass `cacheNull: false` to return `null` without storing it.

## Output Cache

Output cache stores safe server-side response descriptors for GET and HEAD routes.

```ts
app.get("/products", listProducts)
    .outputCache({
        ttlMs: 30000,
        varyByQuery: ["category", "page"],
        varyByHeader: ["accept-language"],
        tags: ["products"],
    });
```

The output cache key uses the route pattern, selected query keys, selected headers,
selected route params, and optional auth partition. It does not use raw full URLs,
`Authorization`, or `Cookie` values.

Authenticated routes are not cached unless they vary by user:

```ts
app.get("/me/dashboard", dashboard)
    .requiresAuth()
    .outputCache({
        ttlMs: 10000,
        varyByUser: true,
        tags: (ctx) => [`user:${ctx.user.sub}:dashboard`],
    });
```

Shared authenticated caching is opt-in. Use it only when every user in the
selected role or claim partition is allowed to see exactly the same response.

```ts
app.get("/admin/summary", adminSummary)
    .requiresAuth({ roles: ["admin"] })
    .outputCache({
        ttlMs: 5000,
        varyByRole: true,
        allowSharedAuthenticated: true,
    });
```

Output cache bypasses unsafe responses:

- methods other than GET/HEAD
- authenticated routes without `varyByUser: true`, unless shared authenticated caching is explicit
- responses with `Set-Cookie`, unless `allowSetCookie: true`
- status codes outside the configured allowlist
- unsupported descriptor kinds, including streams, files, redirects, and custom/native descriptors
- descriptors with functions, symbols, or non-JSON body values
- response bodies larger than `maxBodyBytes`

## Cache Headers

Response cache headers tell clients and proxies what to do. They do not store a server-side
response in Sloppy.

```ts
app.get("/assets/config.json", () => Results.json({ version: 1 }))
    .cacheHeaders({
        cacheControl: "public, max-age=60",
        vary: ["Accept-Encoding"],
        etag: true,
    });

return Results.json(profile).cacheControl("private, max-age=30");
```

Use output cache for server-side reuse. Use cache headers for HTTP client/proxy policy.

## Observability

Registered caches emit low-cardinality app metrics when they are resolved through
`app.services.addCache(...)`:

- `cache.gets.total`
- `cache.hits.total`
- `cache.misses.total`
- `cache.sets.total`
- `cache.removes.total`
- `cache.evictions.total`
- `cache.expired.total`
- `cache.tag_invalidations.total`
- `cache.get_or_create.factory.total`
- `cache.stampede.waiters.total`

Output cache emits `output_cache.requests.total`. Labels use cache names, backend kinds,
route patterns, outcomes, status classes, and bypass reasons. Raw keys, raw URLs, user IDs,
cookies, authorization headers, and values are not labels.

`Health.cache(cache)` reports a cache as unhealthy after disposal and includes safe stats.
