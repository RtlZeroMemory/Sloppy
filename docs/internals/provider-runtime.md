# Provider runtime

Providers connect Sloppy's capability model to actual databases. Each
provider has three layers: a Plan-declared capability, native code that
talks to the database, and a V8 bridge that exposes the API to
JavaScript.

## Layout

```text
include/sloppy/data.h               provider-neutral C contract
src/data/
  common.c                          shared value/result/redaction helpers
  sqlite.c                          SQLite (embedded, single-process)
  postgres.c                        PostgreSQL via libpq (nonblocking)
  sqlserver.c                       SQL Server via ODBC (async)
src/core/provider_executor.c        execution mode dispatch + queueing
src/engine/v8/intrinsics_*          JS bridge for each provider
stdlib/sloppy/data.js               public sql template + value wrappers
stdlib/sloppy/providers/sqlite.js   SQLite-specific public surface
```

## Three layers

```text
JavaScript (handler)
   │  data.sqlite.open / sql`...` / db.query(...)
   ▼
stdlib/sloppy/data.js, stdlib/sloppy/providers/sqlite.js
   │  Validate args, lower sql template, delegate to bridge
   ▼
src/engine/v8/intrinsics_<provider>.cc
   │  Marshal args to Sloppy types, call into provider_executor
   ▼
src/core/provider_executor.c
   │  Pick execution mode (serialized vs async-backed),
   │  honor capability/access checks, deadlines, signals
   ▼
src/data/<provider>.c
   │  Execute against the driver
   ▼
results materialized into Sloppy-owned memory, returned through bridge
```

## Plan-driven setup

Two things in the Plan steer provider runtime:

- `providers[]` — concrete provider instances declared via `app.use(...)`
  or compiler-inferred typed parameters (`Sqlite<"main">`,
  `Postgres<"main">`, `SqlServer<"main">`).
- `capabilities[]` — capability tokens (e.g. `data.main`) with kind
  `database`, a provider name, and an access mode.

At startup, `app_host` cross-checks: every capability with kind
`database` must reference a provider whose runtime feature is active
(`sqlite`, `postgres`, `sqlserver`). Mismatches abort startup with a
diagnostic.

## Execution modes

`provider_executor.c` selects an execution mode per provider:

| Mode             | Used by    | Behavior                                                  |
| ---------------- | ---------- | --------------------------------------------------------- |
| Serialized       | SQLite     | One operation at a time per provider instance, queued     |
| True-async       | PostgreSQL | Nonblocking libpq state machine + Sloppy async backend    |
| True-async (ODBC)| SQL Server | Async ODBC handles when the driver supports them          |
| Blocking pool    | fallback   | Bounded thread pool when no async path exists             |

Mode selection is decided at provider open. SQLite is intentionally
serialized — the underlying engine is single-writer; serializing
operations avoids surprises. PostgreSQL and SQL Server prefer
true-async; the blocking pool is the explicit fallback when async
support is unavailable.

## Connection management

| Provider   | Pool model                                                     |
| ---------- | -------------------------------------------------------------- |
| SQLite     | One writer, optional readers; opened once per provider         |
| PostgreSQL | Bounded connection pool with admission queue                   |
| SQL Server | Bounded connection pool, async-when-available                  |

Pools are sized from provider config (`max-connections`, `min-connections`,
acquisition timeout). Acquisition is fail-fast; calls that can't get a
connection within the deadline raise a typed error.

## Value and result conversion

`src/data/common.c` owns the boundary between provider-native types
(`sqlite3_value`, libpq `PGresult`, ODBC bound buffers) and Sloppy
types. Rules:

- Result rows are materialized into Sloppy-owned memory before the
  driver row is released.
- Strings are validated UTF-8 (where the driver promises UTF-8) or
  copied as bytes.
- Decimal and date/time are normalized to `sql.decimal/date/time/...`
  shapes on the way out.
- Bytes are copied — JS sees a fresh `Uint8Array`.
- `NULL` becomes JS `null`.

The reverse direction is symmetric: `sql.uuid(...)`, `sql.bytes(...)`,
etc., are validated and converted to driver-native parameter
representations.

## Cancellation, deadlines, late completion

Provider operations honor `{ deadline, signal, timeoutMs }` options:

- The executor sets a per-operation deadline.
- Cancellation cancels in-flight driver work where the driver supports
  it (libpq `PQcancel`, ODBC `SQLCancel`).
- A late completion (driver returns *after* deadline) does cleanup
  only — the JS Promise has already settled with a cancellation
  error. There is no double-settle.

Shutdown drains in-flight operations within their deadlines, then
forces close.

## Redaction

`src/data/common.c` redacts everything that flows into diagnostics:

- Connection strings and credentials are never embedded in error
  messages.
- Parameter values are stripped from query failure diagnostics by
  default.
- Driver-specific error metadata (PG state codes, ODBC SQLSTATE) is
  preserved because it's not credential-bearing.

Provider-specific redaction lives in each provider's `*.c` (e.g.
`sl_pg_safe_config_hint` in `postgres.c`).

## Tests

| Lane                     | What it covers                                       |
| ------------------------ | ---------------------------------------------------- |
| Native unit tests        | Driver setup, value conversion, redaction            |
| Conformance              | Common Db API behavior across providers              |
| V8 bridge tests (V8-gated)| JS-visible behavior end-to-end                      |
| Live PostgreSQL (opt-in) | Real database with connection string                 |
| Live SQL Server (opt-in) | Real database + ODBC driver                          |
| Stress / torture (opt-in)| Long-running workload for leak/cancellation behavior |

Live and V8 lanes are opt-in. Default CI covers native and conformance.

## Adding a provider

The shape:

1. Add a runtime feature (`src/core/features.c`).
2. Implement the native provider against the `SlDb` contract in
   `include/sloppy/data.h` (open, exec, query, transaction, close,
   redact, cancel).
3. Wire execution mode in `provider_executor.c`.
4. Add the V8 intrinsic under `src/engine/v8/intrinsics_<name>.cc`.
5. Surface the JS API in `stdlib/sloppy/providers/<name>.js`.
6. Plan extraction in `compiler/src/sloppyc.rs` (provider name, types).
7. Tests across the lanes above.

This is intentionally not a small lift — providers live in the trusted
boundary, so adding one means landing each layer with its own evidence.
