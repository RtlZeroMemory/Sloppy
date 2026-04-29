# Data Providers

This directory contains provider-specific runtime support.

Implemented now:

- `src/data/sqlite.c` implements the first native SQLite provider boundary.
- `src/data/postgres.c` implements the native PostgreSQL/libpq provider boundary.
- SQLite headers are included only in that provider-specific file.
- libpq headers are included only in the PostgreSQL provider-specific file.
- Native tests cover `:memory:` open/close, `exec`, `query`, `queryOne`, parameter binding,
  transaction commit/rollback, and provider diagnostics.
- PostgreSQL default tests cover option validation, redaction, bridge diagnostics, and
  skipped-by-default live coverage. When `SLOPPY_POSTGRES_TEST_URL` is set, they connect to
  PostgreSQL and cover exec/query/queryOne, transactions, and tiny pool acquire/release.

Still deferred:

- JavaScript stdlib-to-native database intrinsics;
- SQL Server provider;
- production pooling, migrations, async worker offload, cancellation/deadlines, TLS option
  hardening, arrays, JSON, and blob support.

The current source of truth is `docs/data-providers.md`.
