# Data Module

## Status

Bootstrap data/capabilities foundation implemented. The first native SQLite provider is
implemented for C/runtime tests; JavaScript stdlib-to-native database intrinsics are still
planned / not implemented yet.

## Purpose

Provide common data APIs and provider integrations for SQLite, PostgreSQL, and SQL Server.

## Scope

Common data API, query template lowering, transactions, provider resources, capabilities,
and diagnostics.

## Non-goals

No database dependencies before the relevant provider phase.

## Public/Internal API

Implemented bootstrap API:

- `sql` tagged template lowering helper.
- `data.lowerQueryTemplate(strings, values, options)` for direct lowering tests.
- `data.createFakeProvider(definition)` for tests/examples.
- fake provider methods: `query`, `queryOne`, `exec`, and `transaction`.
- `builder.capabilities.addDatabase(token, options)` and module `.capabilities(...)`
  metadata declarations.
- `data.sqlite` provider metadata and `data.sqlite.open(options)` as the future stdlib
  entry point. It validates options and fails honestly until the native bridge exists.

Implemented native SQLite API:

- `include/sloppy/data_sqlite.h` and `src/data/sqlite.c`;
- caller-owned `SlSqliteConnection` and `SlSqliteTransaction` wrappers;
- `sl_sqlite_open_options_memory`, `sl_sqlite_open`, and `sl_sqlite_close`;
- `sl_sqlite_exec`, `sl_sqlite_query`, and `sl_sqlite_query_one`;
- transaction begin/commit/rollback plus transaction-scoped exec/query/queryOne helpers.

## Ownership/Lifetime Rules

Current fake providers own only JavaScript test/example callbacks and debug event arrays.
Fake transactions close their transaction object after commit/rollback and reject use after
close. Native SQLite connections are caller-owned C wrappers and must be closed
deterministically through `sl_sqlite_close`; prepared statements are finalized on every
path. Future JS-visible real connections, statements, pools, and transactions are
resource-table-owned and scoped explicitly.

## Invariants

Template query APIs parameterize by default. Lowered query descriptors preserve text and
parameters separately. Native SQLite accepts the existing `?` placeholder lowering path and
binds `null`, text, integer, float, and boolean values without interpolation.
Provider-specific APIs stay namespaced.

## Diagnostics

Implemented JavaScript errors cover invalid query template usage, fake provider missing
methods, duplicate/missing capability tokens, invalid database capability metadata,
transaction callback misuse, nested transactions, use after closed transaction scope, and
the missing stdlib native SQLite bridge.

Native SQLite diagnostics use `SL_DIAG_SQLITE_PROVIDER_ERROR` and
`SL_DIAG_DATABASE_UNSUPPORTED_VALUE`. They include provider `sqlite`, operation, SQLite
error text where available, and SQL text without parameter values.

## Tests

`bootstrap.stdlib.data_foundation` executes the ESM stdlib with Node when available and
covers capability metadata, query lowering, fake provider method dispatch, transaction
commit/rollback, rejected async callbacks, nested transaction rejection, use after close,
module/service integration, and the honest `data.sqlite.open(...)` bridge-unavailable
path.

`data.sqlite.provider` is a native CTest target that links SQLite and covers in-memory
open/close, use after close, exec, parameterized insert, query row shape, queryOne found and
missing behavior, primitive parameter types, unsupported parameter diagnostics, transaction
commit/rollback, nested transaction rejection, transaction use after complete, invalid SQL,
missing table diagnostics, and invalid open diagnostics.

## Source Docs

- `docs/data-providers.md`;
- `docs/concurrency.md`;
- `docs/modularity.md`;
- `docs/testing-strategy.md`;
- ADR 0010.

## Open Questions

- Exact row/result JS shape once native stdlib intrinsics exist.
