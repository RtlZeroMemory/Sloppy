# Provider Runtime Internals

## Where It Lives

- `src/data/sqlite.c`
- `src/data/postgres.c`
- `src/data/sqlserver.c`
- `src/engine/v8/intrinsics_sqlite.cc`
- `src/engine/v8/intrinsics_postgres.cc`
- `src/engine/v8/intrinsics_sqlserver.cc`

## Model

Native providers implement database operations and diagnostics. V8 intrinsics
install provider bridges for JavaScript app execution. Live PostgreSQL and SQL
Server tests require external services and environment variables.

## Invariants

Provider diagnostics must redact connection strings and credentials. Live
provider skips are not pass evidence.
