# Providers Reference

Current provider kinds are:

- `sqlite`
- `postgres`
- `sqlserver`

## Provider Surfaces

Provider support is split across several surfaces. A provider may be visible to
the compiler before every runtime path is available.

| Surface | Current provider shape | Current status |
| --- | --- | --- |
| Framework descriptor registration | `app.use(sqlite("main", ...))` from `sloppy/providers/sqlite` | SQLite descriptor admission only |
| Static provider handle | `app.provider("sqlite:main")` | SQLite generated bridge path; non-SQLite static provider handles are diagnostic-only in current fixtures |
| Typed handler injection | `Sqlite<"main">`, `Postgres<"main">`, `SqlServer<"main">` | Compiler metadata and generated injection wrappers exist; runtime execution depends on active bridge, config, and live service setup |
| Runtime data API | `data.sqlite`, `data.postgres`, `data.sqlserver` from `sloppy/data` | Provider-specific runtime APIs with V8/native/live requirements |
| Native and service checks | provider native tests and `test-live-*.ps1` scripts | SQLite embedded by default; PostgreSQL/SQL Server service checks are opt-in |
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
| `Postgres<"main">` | emits `postgres/main` provider and `data.main` capability requirements | `Sloppy__Providers__postgres__main__connectionString`, active PostgreSQL bridge, and live PostgreSQL service setup |
| `SqlServer<"main">` | emits `sqlserver/main` provider and `data.main` capability requirements | `Sloppy__Providers__sqlserver__main__connectionString`, active SQL Server bridge, and an ODBC driver with async support |

When documenting PostgreSQL or SQL Server, name the typed-injection/runtime API
surface and the runtime path being used.

Generated typed provider wrappers read connection-string environment variables
at request time. They do not embed connection-string values in `app.js` or
`app.plan.json`.

## Runtime Open Contracts

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
- optional `maxConnections`: integer `1..16` (default `1`)

### SQL Server

`data.sqlserver.open(options)` requires:

- `connectionString` (non-empty string)
- optional `access`: `read` or `readwrite` (default `readwrite`)
- optional `maxConnections`: integer `1..16` (default `1`)

## Bridge Availability

Provider APIs require active runtime bridge namespaces under `globalThis.__sloppy.data`:

- sqlite: `globalThis.__sloppy.data.sqlite`
- postgres: `globalThis.__sloppy.data.postgres`
- sqlserver: `globalThis.__sloppy.data.sqlserver`

If unavailable, calls fail with `SLOPPY_E_UNAVAILABLE_RUNTIME_FEATURE` and redacted diagnostics.

V8 bridge availability is separate from native provider availability. A provider
can be available to native tests before the JavaScript bridge or live service is
configured on the current machine.

## Transactions

All three providers expose callback transactions with these enforced rules:

- nested transaction calls are rejected
- using transaction object after callback settles is rejected
- closing a connection during an active transaction is rejected

## Redaction Helpers

Redaction behavior includes:

- postgres: `redactConnectionString(...)`
- sqlserver: `redactConnectionString(...)`
- sqlserver doctor metadata redacts sensitive fields (`PWD`, `Password`, access-token fields)

## Runtime Limits

- Compiler-generated static provider handler bridge is sqlite-only;
  non-sqlite static provider handlers fail with
  `SLOPPYC_E_UNSUPPORTED_PROVIDER_BRIDGE`.
- Fake provider APIs (`data.createFakeProvider`) validate shape and behavior
  contracts only. Live database behavior uses provider integration checks.
- Missing service dependencies are reported as skipped or unavailable.
  Service-backed providers, V8 execution, and benchmarks use separate checks.
