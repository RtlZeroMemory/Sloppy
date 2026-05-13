# Cache Reference

## Imports

```ts
import { Cache, SloppyCacheError } from "sloppy";
```

The root import and `sloppy/cache` export the same `Cache` object.

## Factory API

| API | Backend | Notes |
| --- | --- | --- |
| `Cache.memory(nameOrOptions?, options?)` | memory | Bounded per-process cache. |
| `Cache.sqlite(dbOrOptions, options?)` | SQLite | Requires a real `data.sqlite.open(...)` connection. |
| `Cache.postgres(dbOrOptions, options?)` | PostgreSQL | Requires a real `data.postgres.open(...)` connection. |
| `Cache.sqlServer(dbOrOptions, options?)` | SQL Server | Alias: `Cache.sqlserver(...)`. |
| `Cache.distributed(kind, db, options?)` | provider-backed | `kind` is `sqlite`, `postgres`, or `sqlserver`. |
| `Cache.hybrid(name, options)` | hybrid | Requires `memory` and distributed cache instances. |
| `Cache.noop(name?)` | test helper | Always misses; useful only in tests. |

## Cache Methods

| Method | Behavior |
| --- | --- |
| `get(key, schemaOrOptions?)` | Returns the cached value or `undefined` on miss/expiry. |
| `set(key, value, options?)` | Stores a JSON-serializable value. |
| `remove(key)` / `delete(key)` / `invalidate(key)` | Removes one key. |
| `has(key)` | Returns `true` when `get(key)` would hit. |
| `getOrCreate(key, options, factory)` | Cache-aside helper with local in-flight coalescing. |
| `invalidateTag(tag)` | Invalidates one tag. |
| `invalidateTags(tags)` | Invalidates any entry matching one of the tags. |
| `clear(options?)` | Clears this namespace; distributed `dangerouslyClearAll` clears the whole table. |
| `cleanup(options?)` | Removes expired entries where the backend supports cleanup. |
| `stats()` | Returns safe counters and backend metadata. |
| `dispose()` | Disposes the cache and rejects later operations. |

## Entry Options

```ts
{
  ttlMs?: number;
  absoluteExpiration?: Date | string;
  slidingExpirationMs?: number;
  tags?: string[];
  schema?: Schema;
  staleWhileRevalidateMs?: number;
  stampedeProtection?: boolean;
  namespace?: string;
  cacheNull?: boolean;
  signal?: AbortSignal;
}
```

`ttlMs`, `slidingExpirationMs`, and `staleWhileRevalidateMs` are finite integer millisecond
values. `absoluteExpiration` must parse to a valid timestamp. `schema` must be a Sloppy
`Schema` value.

`staleWhileRevalidateMs` is accepted in the options shape for compatibility with cache-aside
policy objects, but the current cache returns fresh hits and normal misses; it does not serve
stale entries.

## Names, Keys, Tags

- Cache names are non-empty stable strings up to 128 characters.
- Cache keys are non-empty strings without control characters. The default max length is 512.
- Tags are non-empty strings without control characters. The default max length is 128.
- `Cache.key(...parts)` URL-encodes each part and joins them with `:`.
- `Cache.tags(...values)` flattens arrays, validates tags, deduplicates, and freezes the result.
- `Cache.keyHash(key)` returns a stable non-secret hash for diagnostics.
- `Cache.token(name)` returns the service token, for example `cache.main`.

## Distributed SQL

Distributed caches create a single entries table by default:

- `namespace`
- `cache_key`
- `value_json`
- `created_at`
- `updated_at`
- `expires_at`
- `sliding_expiration_ms`
- `tags_json`

SQLite and PostgreSQL use provider-native upsert syntax. SQL Server uses update-then-insert
with ODBC `?` parameters. Provider values are always bound as parameters; table names must
be simple SQL identifiers.

## Output Cache Options

```ts
{
  ttlMs: number;
  cacheName?: string;
  varyByQuery?: string[] | "all";
  varyByHeader?: string[];
  varyByRouteParams?: string[];
  varyByUser?: boolean;
  varyByClaim?: string[];
  varyByRole?: boolean;
  tags?: string[] | ((ctx) => string[]);
  statusCodes?: number[];
  maxBodyBytes?: number;
  allowSetCookie?: boolean;
  allowAuthenticated?: boolean;
  allowSharedAuthenticated?: boolean;
}
```

Default cacheable statuses are `200`, `203`, and `204`. The default maximum body size is
1 MiB, counted as UTF-8 bytes. Authenticated output cache requires `varyByUser: true`.
Shared authenticated caching by role or claim requires `allowSharedAuthenticated: true`
and should only be used when every user in that partition may receive the same body.
`allowAuthenticated: true` without a user partition or explicit shared partition is rejected.

Output cache stores Sloppy result descriptors for JSON, text, bytes, and empty/status-only
results. Streams, files/static results, redirects, custom/native descriptors, and descriptors
with functions, symbols, or non-JSON body values bypass.

## Metrics

Cache metric labels are intentionally low-cardinality:

- `cache`: cache name
- `backend`: `memory`, `sqlite`, `postgres`, `sqlserver`, `hybrid`, or `noop`
- `operation`: bounded operation name
- output cache adds route pattern, outcome, status class, and bypass reason

Raw cache keys, raw URLs, user IDs, cookie values, authorization values, SQL parameters, and
cached values are not metric labels.
