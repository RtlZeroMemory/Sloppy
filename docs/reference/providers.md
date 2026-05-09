# Providers Reference

Current provider kinds are:

- `sqlite`
- `postgres`
- `sqlserver`

## API Surfaces

Provider usage exists in two separate surfaces.

1. Framework descriptor registration (`app.use(...)`):
   - source: `sloppy/providers/sqlite`
   - current app-level descriptor support: sqlite only
2. Runtime data APIs (`sloppy/data`):
   - `data.sqlite`
   - `data.postgres`
   - `data.sqlserver`

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

## Runtime-Lane Limits

- Compiler-generated provider handler bridge is sqlite-only; non-sqlite provider handlers fail with `SLOPPYC_E_UNSUPPORTED_PROVIDER_BRIDGE`.
- Fake provider APIs (`data.createFakeProvider`) validate shape and behavior contracts only; they are not live DB evidence.
- Missing live-provider dependencies are reported as skipped or unavailable. live-provider,
  V8, and benchmark lanes are separate evidence types and do not imply one another.
