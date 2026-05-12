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
  sqlite.c                          SQLite direct C provider
  postgres.c                        PostgreSQL direct C provider via libpq
  sqlserver.c                       SQL Server direct C provider via ODBC
src/core/provider_executor.c        execution mode dispatch + queueing
src/engine/v8/intrinsics_*          JS bridge for each provider
stdlib/sloppy/data.js               public sql template + value wrappers
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
   │  Marshal args, check provider handles/capabilities,
   │  use provider_executor where that bridge is wired
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

Provider execution is currently split between the shared executor and
provider-specific V8 bridges:

| Mode             | Used by    | Behavior                                                  |
| ---------------- | ---------- | --------------------------------------------------------- |
| Serialized       | SQLite V8  | One operation at a time through `provider_executor`       |
| Direct blocking  | C providers| Caller-owned connection APIs under `src/data/*`           |
| True-async       | PostgreSQL V8 | Nonblocking libpq state machine + Sloppy async backend |
| True-async (ODBC)| SQL Server V8 | Async ODBC handles when the driver supports them       |

SQLite is intentionally serialized in the V8 bridge because the provider
instance is single-writer. PostgreSQL and SQL Server V8 bridges own their
driver state machines directly rather than going through a generic database
provider vtable.

## Connection management

| Provider   | Pool model                                                     |
| ---------- | -------------------------------------------------------------- |
| SQLite     | One connection per handle; V8 operations are serialized        |
| PostgreSQL | Bounded pool; acquisition is fail-fast                         |
| SQL Server | Bounded pool; acquisition is fail-fast                         |

PostgreSQL and SQL Server pools are sized by `maxConnections`. They do not
currently expose a wait queue, idle pruning, or acquisition timeout. If every
connection is busy, acquisition fails immediately.

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

The JavaScript API accepts `{ deadline, signal, timeoutMs }` operation options.
A signal already aborted, an expired deadline, or a zero timeout rejects before
native work starts. A finite `deadline` is reduced to the remaining timeout
budget before dispatch.

Native row-returning bridge calls pass `timeoutMs` to driver interruption:

- SQLite `query` and `queryRaw` install a progress handler and fail with a
  deadline diagnostic when it interrupts execution.
- PostgreSQL `query` and `queryRaw` start a timeout watcher that calls
  `PQcancel`.
- SQL Server `query` and `queryRaw` set the ODBC statement query timeout and
  call `SQLCancelHandle` from the timeout watcher.

Already-aborted signals are honored before dispatch. In-flight driver
interruption is timeout/deadline based.

## Result Bounds

All providers expose a bounded materialization path for `query` and `queryRaw`:

- The default provider cap is `128` rows.
- `maxRows` can raise or lower the per-call cap.
- Exceeding the cap fails the query rather than truncating results.
- `queryOne` materializes at most one row.

SQLite and SQL Server enforce bounds during row fetch/materialization.
PostgreSQL V8 uses libpq single-row mode for bounded `query` and `queryRaw`
operations, so exceeding `maxRows` fails while rows are still being received.
The public data-provider API returns materialized result sets rather than cursor
streams or incremental JSON row streams.

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

Live and V8 lanes are opt-in. Default CI covers native and conformance. Missing
Docker, missing drivers, or unsupported async driver behavior is an unavailable
live-provider lane, not a pass.

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
