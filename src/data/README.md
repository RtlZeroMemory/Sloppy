# Data Providers

This directory contains provider-specific runtime support.

Implemented now:

- `src/data/sqlite.c` implements the first native SQLite provider boundary.
- SQLite headers are included only in that provider-specific file.
- Native tests cover `:memory:` open/close, `exec`, `query`, `queryOne`, parameter binding,
  transaction commit/rollback, and provider diagnostics.

Still deferred:

- JavaScript stdlib-to-native database intrinsics;
- PostgreSQL and SQL Server providers;
- pooling, migrations, async worker offload, cancellation/deadlines, and blob support.

The current source of truth is `docs/data-providers.md`.
