# Providers Reference

Current provider kinds are:

- `sqlite`
- `postgres`
- `sqlserver`
- `redis` for the first-party Redis client and Redis-backed cache provider

## Provider Surfaces

Provider support is split across several surfaces. A provider may be visible to
the compiler before every runtime path is available.

| Surface | Current provider shape | Current status |
| --- | --- | --- |
| Framework descriptor registration | `app.use(sqlite("main", ...))` from `sloppy/providers/sqlite` | SQLite descriptor admission only |
| Static provider handle | `app.provider("sqlite:main")` | SQLite generated bridge path; non-SQLite static provider handles are diagnostic-only in current fixtures |
| Typed handler injection | `Sqlite<"main">`, `Postgres<"main">`, `SqlServer<"main">` | Compiler metadata and generated injection wrappers exist; runtime execution depends on active bridge, config, and live service setup |
| Runtime data API | `data.sqlite`, `data.postgres`, `data.sqlserver` from `sloppy/data` | Provider-specific runtime APIs with V8/native/live requirements |
| Runtime Redis API | `Redis.client(...)`, `Cache.redis(...)` from `sloppy` | First-party RESP2 Redis client over the Sloppy network bridge; cache provider is Redis-backed only |
| Migrations | `Migrations` from `sloppy/data`, `sloppy db status`, `sloppy db migrate` | SQLite, PostgreSQL, and SQL Server migration execution; PostgreSQL/SQL Server are optional and require live provider configuration only when used |
| TestServices | `TestServices.postgres()`, `TestServices.sqlServer()`, `TestServices.redis()` | Experimental Docker-backed real dependency tests; opt-in and provider/client-bridge-gated |
| Native and service checks | provider native tests and `test-live-*.ps1` scripts | SQLite embedded by default; PostgreSQL/SQL Server dependency and service checks are opt-in |
| V8 provider bridge checks | `conformance.<provider>.bridge_live` | Exercises JavaScript provider calls through a V8-enabled runtime |

## Framework Descriptor Contract

`sloppy/providers/sqlite` exports:

```ts
sqlite(name: string, options?: { database?: string })
```

Descriptor constraints:

- `name` must be non-empty and match `[A-Za-z0-9_.-]+`
- options must be a plain object when provided
- `options.database` must be a string when provided

`app.use(...)` currently rejects non-sqlite descriptors.

## Static Provider Handle Contract

Static handle source uses provider tokens:

```ts
const db = app.provider("sqlite:main");
```

Current generated provider bridge support is SQLite-only. Compiler fixtures
show that non-SQLite static provider handles such as `postgres:analytics`
produce the unsupported provider bridge diagnostic.

The compiler emits this diagnostic before generated artifacts can imply a live
PostgreSQL or SQL Server static handle path.

## Typed Handler Injection

Typed provider parameters are compiler-recognized metadata shapes:

```ts
app.get("/users", async (db: Postgres<"main">) => {
  return Results.json(await db.query("select id, name from users"));
});
```

Current behavior by provider:

| Marker | Compiler metadata | Runtime requirements |
| --- | --- | --- |
| `Sqlite<"main">` | emits `sqlite/main` provider and `data.main` capability requirements | active SQLite bridge/config; V8-enabled runtime for handler execution |
| `Postgres<"main">` | emits `postgres/main` provider and `data.main` capability requirements | `Sloppy__Providers__postgres__main__connectionString`, active PostgreSQL bridge, PostgreSQL client support, and live PostgreSQL service setup |
| `SqlServer<"main">` | emits `sqlserver/main` provider and `data.main` capability requirements | `Sloppy__Providers__sqlserver__main__connectionString`, active SQL Server bridge, Microsoft ODBC Driver 17 or 18, and live SQL Server service setup |

When documenting PostgreSQL or SQL Server, name the typed-injection/runtime API
surface and the runtime path being used.

Generated typed provider wrappers read connection-string environment variables
at request time. They do not embed connection-string values in `app.js` or
`app.plan.json`.

## Runtime Open Contracts

### Redis

`Redis.client(name, options)` requires a Redis URL and the Sloppy outbound
network bridge:

- `url`: `redis://` or `rediss://`
- optional `password`: string or `Redis.Secret`
- optional `database`: integer `0..15`
- optional bounded pool and timeout options

`Cache.redis(...)` builds on this client. It does not provide a fake cache or
memory fallback when Redis is unavailable.

### SQLite

`data.sqlite.open(options)` requires:

- `database` or `path` (non-empty string)
- `capability` (non-empty string)
- optional `access`: `read`, `write`, `readwrite` (default `readwrite`)

If both `database` and `path` are provided, they must match.

`data.sqlite(name)` normalizes `name` to a provider token (`data.<name>` unless already dotted).

### PostgreSQL

`data.postgres.open(options)` requires:

- `connectionString` (non-empty string)
- optional `access`: `read` or `readwrite` (default `readwrite`)
- optional `maxConnections`: integer `1..256` (default `1`)

### SQL Server

`data.sqlserver.open(options)` requires:

- `connectionString` (non-empty string)
- optional `access`: `read` or `readwrite` (default `readwrite`)
- optional `maxConnections`: integer `1..256` (default `1`)

## Bridge Availability

Provider APIs require active runtime bridge namespaces under `globalThis.__sloppy.data`:

- sqlite: `globalThis.__sloppy.data.sqlite`
- postgres: `globalThis.__sloppy.data.postgres`
- sqlserver: `globalThis.__sloppy.data.sqlserver`

Redis APIs require active runtime bridge namespaces under `globalThis.__sloppy.net`:

- `connect` for `redis://`
- `connectTls` for `rediss://`

If unavailable, calls fail with `SLOPPY_E_UNAVAILABLE_RUNTIME_FEATURE` and redacted diagnostics.

V8 bridge availability is separate from native provider availability. A provider
can be available to native tests before the JavaScript bridge or live service is
configured on the current machine.

## Transactions

All three providers expose callback transactions with these enforced rules:

- nested transaction calls are rejected
- using transaction object after callback settles is rejected
- closing a connection during an active transaction is rejected

## Migrations And Health

SQLite, PostgreSQL, and SQL Server expose first-party migration support through
`Migrations.apply` and `Migrations.status` from `sloppy/data`. The CLI mirrors
that contract with `sloppy db status` and `sloppy db migrate`.

Migration behavior:

- migration files come from `migrations/*.sql`-style project paths
- files apply in lexical order
- each file runs in its own transaction
- `_sloppy_migrations` records `id`, `name`, `hash`, and `appliedAt`
- changed applied hashes fail instead of being silently accepted
- repeated runs skip unchanged files

`ProviderHealth.check(db, { provider })` runs a small readiness query. For
PostgreSQL and SQL Server, equivalent live health checks require configured
live providers and stay outside the default test lane.

PostgreSQL and SQL Server CLI migrations are optional feature paths. They
require live connection strings and the matching provider dependency only when
the selected migration provider uses that database. For generated provider
metadata, `sloppy db` reads
`Sloppy__Providers__postgres__<name>__connectionString` or
`Sloppy__Providers__sqlserver__<name>__connectionString` unless the Plan
provider carries an explicit `configKey`.

## TestServices Provider Descriptors

`TestServices.postgres()` and `TestServices.sqlServer()` are experimental and start real Docker
containers, wait for provider-backed `select 1` readiness, and expose
`provider()` for `TestHost.create(..., { providers })`.

If the matching provider bridge is unavailable, `provider()` and service
startup fail with `SLOPPY_E_TESTSERVICES_PROVIDER_UNAVAILABLE`. The helper does
not return a fake provider and does not fall back to in-memory data.

Artifact/package tests can pass `service.env()` to `TestHost.fromArtifacts`
or `TestHost.fromPackage`; the runtime that consumes that environment still
needs the provider bridge and driver support.

`TestServices.redis()` starts a real Redis container, waits for `PING` through
the first-party Redis client, and exposes `client()`, `env()`, `flush()`, and
`reset()`. If the Redis network bridge is unavailable, service startup fails;
the helper does not provide an in-memory substitute.

## Result Modes

All three providers expose object-row and raw positional result modes through
the runtime data API:

- `query(...)` returns object rows by default.
- `query(..., { mode: "raw" })` and `queryRaw(...)` return `{ mode, columns, columnNames, rows }`.
- `queryCursor(...)` returns an async iterable object-row cursor.
- `queryRawCursor(...)` returns an async iterable raw positional cursor.
- `stream(...)` is a connection-level alias for `queryCursor(...)`.
- `queryOne(...)` returns a single object row or `null`.

Raw mode preserves duplicate column names and positional values. Object mode
uses normal JS object property semantics, so the last duplicate column name wins.

`query(...)` and `queryRaw(...)` are bounded. The default cap is 128 materialized
rows, and operation option `{ maxRows }` adjusts the cap for one call. Providers
fail with an exceeded-max-rows diagnostic instead of silently truncating.

Cursors are the large-result API. They do not use the 128-row materialization
cap by default, but accept `{ maxRows }` when a stream should be bounded by the
caller. Cursors expose `close()`, `closed`, `provider`, `mode`, `columns`, and
`columnNames`; closing is idempotent and `next()` after close fails
deterministically. Early loop break, consumer errors, provider close, runtime
shutdown, and transaction teardown release or invalidate active cursor
resources.

Provider cursor ownership:

- SQLite keeps the prepared statement active and steps rows incrementally.
- PostgreSQL uses libpq single-row mode and pins the pool connection until the
  cursor closes or reaches end-of-stream.
- SQL Server keeps the ODBC statement active and fetches rows incrementally
  with `SQLFetch`.

The public API exposes Sloppy cursor objects only. Driver pointers, statement
handles, libpq handles, and ODBC handles are never exposed to JavaScript.

## Redaction Helpers

Redaction behavior includes:

- postgres: `redactConnectionString(...)`
- sqlserver: `redactConnectionString(...)`
- sqlserver doctor metadata redacts sensitive fields (`PWD`, `Password`, access-token fields)

## Runtime Limits

- Compiler-generated static provider handler bridge is sqlite-only;
  non-sqlite static provider handlers fail with
  `SLOPPYC_E_UNSUPPORTED_PROVIDER_BRIDGE`.
- `deadline`, `signal`, and `timeoutMs` provider operation options are checked
  before dispatch. Native `query`, `queryRaw`, and cursor bridge calls pass
  finite timeout budgets to driver-level interruption. Signals are a
  pre-dispatch cancellation mechanism for data providers.
- Response streaming is currently an async-iterator integration point. Sloppy
  does not provide a cursor-to-HTTP helper that first materializes every row.
  Cursor column metadata is stable for future native JSON streaming.
- PostgreSQL and SQL Server pools are bounded and fail fast under full
  saturation. They do not expose a public wait queue, acquire timeout, idle
  pruning, or max-lifetime policy.
- Fake provider APIs (`data.createFakeProvider`) validate shape and behavior
  contracts only. Live database behavior uses provider integration checks.
- Missing optional provider dependencies are reported as provider-specific
  guidance, not as a broken Sloppy install for apps that do not use that
  provider. Service-backed providers, V8 execution, and benchmarks use separate
  checks. Optional live-provider checks may be skipped or unavailable on a
  default machine.
