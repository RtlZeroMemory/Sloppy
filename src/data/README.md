# Data Providers

This directory contains provider-specific runtime support.

Implemented now:

- `src/data/common.c` implements the provider-neutral Db value, statement, result, and
  redaction helpers declared by `include/sloppy/data.h`.
- `src/data/sqlite.c` implements the first native SQLite provider boundary.
- `src/data/postgres.c` implements the native PostgreSQL/libpq provider boundary.
- `src/data/sqlserver.c` implements the native SQL Server/ODBC provider boundary.
- SQLite headers are included only in that provider-specific file.
- libpq headers are included only in the PostgreSQL provider-specific file.
- ODBC headers are included only in the SQL Server provider-specific file.
- Native tests cover `:memory:` and file database open/close, `exec`, `query`, `queryOne`,
  parameter binding, explicit SQLite text encoding for JSON/date/time-like values,
  transaction commit/rollback, and provider diagnostics.
- PostgreSQL default tests cover option validation, redaction, bridge diagnostics, and
  skipped-by-default live coverage. When `SLOPPY_POSTGRES_TEST_URL` is set, they connect to
  PostgreSQL and cover exec/query/queryOne, cursor streaming, transactions, and tiny pool
  acquire/release.
- SQL Server default tests cover option validation, redaction, driver-name parsing,
  missing-driver diagnostics, use-after-close behavior, unsupported values, and tiny pool
  state behavior. When `SLOPPY_SQLSERVER_TEST_CONNECTION_STRING` is set, they connect
  through ODBC and cover exec/query/queryOne, cursor streaming, transactions, invalid SQL
  diagnostics, and tiny pool acquire/release.

Not part of this provider layer:

- TLS option hardening, richer provider-specific value policy, prepared statement caches,
  PostgreSQL array mapping policy beyond the current JS bridge, and SQL Server TVP/bulk
  support.

Provider result materialization is bounded. The default query cap is 128 rows;
JavaScript `query` and `queryRaw` calls can pass `maxRows` to lower or raise the
per-call cap, and providers fail instead of truncating when the cap is exceeded.
Large result sets use public cursor APIs instead of unbounded materialization:
`queryCursor`, `queryRawCursor`, and the `stream` alias. SQLite cursors keep a
prepared statement active and step rows incrementally; PostgreSQL V8 cursors use
libpq single-row mode; SQL Server V8 cursors keep an ODBC statement active and
fetch incrementally with `SQLFetch`. Cursor resources pin their provider
connection until closed or exhausted, expose stable column metadata, and accept
optional `maxRows` for caller-owned stream caps.
JavaScript `query` and `queryRaw` calls can also pass finite timeout budgets:
SQLite interrupts through the `sqlite3_progress_handler` path in `src/data/sqlite.c`.
PostgreSQL timeout/cancel is implemented in the V8 bridge through `PQcancel`
in `src/engine/v8/intrinsics_postgres.cc`, and SQL Server timeout/cancel is
implemented in the V8 bridge through ODBC APIs in
`src/engine/v8/intrinsics_sqlserver.cc`.

Future JavaScript-visible provider handles must use `SlResourceId` entries in the core
resource table. Provider pointers and driver handles must not be exposed to JavaScript.

The current public provider reference is `docs/reference/providers.md`; runtime
implementation invariants are in `docs/internals/provider-runtime.md`.
