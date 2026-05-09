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
  PostgreSQL and cover exec/query/queryOne, transactions, and tiny pool acquire/release.
- SQL Server default tests cover option validation, redaction, driver-name parsing,
  missing-driver diagnostics, use-after-close behavior, unsupported values, and tiny pool
  state behavior. When `SLOPPY_SQLSERVER_TEST_CONNECTION_STRING` is set, they connect
  through ODBC and cover exec/query/queryOne, transactions, invalid SQL diagnostics, and
  tiny pool acquire/release.

Still deferred:

- broader JavaScript PostgreSQL and SQL Server provider intrinsics;
- migrations, cancellation/deadline hardening, TLS option hardening, provider array
  mapping, richer provider-specific value policy, and SQL Server TVP/bulk support.

Future JavaScript-visible provider handles must use `SlResourceId` entries in the core
resource table. Provider pointers and driver handles must not be exposed to JavaScript.

The current public provider reference is `docs/reference/providers.md`; runtime
implementation invariants are in `docs/internals/provider-runtime.md`.
