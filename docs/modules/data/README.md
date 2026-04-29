# Data Module

## Status

Bootstrap data/capabilities foundation implemented. Native SQLite, PostgreSQL, and SQL
Server providers are implemented for C/runtime tests. MAIN1-08 implements the first
JavaScript stdlib-to-native database bridge for SQLite in V8-enabled runtime contexts.

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
- `data.sqlite` provider metadata and `data.sqlite.open(options)`. It validates options
  and returns a safe SQLite connection wrapper when the V8 runtime installs
  `__sloppy.data.sqlite`; otherwise it fails honestly with bridge-unavailable.
- `data.postgres` provider metadata, `$1` placeholder style, redaction helper, and
  `data.postgres.open(options)` as the future stdlib entry point. It validates options and
  fails honestly until the native bridge exists.
- `data.sqlserver` provider metadata, ODBC `?` placeholder style, redaction and doctor
  helpers, and `data.sqlserver.open(options)` as the future stdlib entry point. It
  validates options and fails honestly until the native bridge exists.
- Plan v1 alpha `dataProviders` and `capabilities` sections can describe provider and
  authority metadata for startup validation/audit. MAIN1-10 adds a native capability
  registry and check functions that future JS-native bridge calls must run before provider
  work. Direct provider APIs remain low-level primitives without capability context.

Implemented native SQLite API:

- `include/sloppy/data_sqlite.h` and `src/data/sqlite.c`;
- caller-owned `SlSqliteConnection` and `SlSqliteTransaction` wrappers;
- `sl_sqlite_open_options_memory`, `sl_sqlite_open`, and `sl_sqlite_close`;
- `sl_sqlite_exec`, `sl_sqlite_query`, and `sl_sqlite_query_one`;
- transaction begin/commit/rollback plus transaction-scoped exec/query/queryOne helpers.

Implemented SQLite JS bridge API:

- internal V8 intrinsics under `__sloppy.data.sqlite`;
- V8 layering: `engine_v8.cc` owns the provider-neutral engine core, `intrinsics.cc`
  aggregates provider intrinsic registration, and `intrinsics_sqlite.cc` owns SQLite
  argument conversion, row materialization, resource lookup, cleanup, and provider calls.
  Future PostgreSQL/SQL Server bridges must add sibling intrinsic modules instead of
  expanding `engine_v8.cc`;
- `data.sqlite.open({ path })` wrapper that stores only an opaque `SlResourceId` handle;
- connection methods `exec(sql, params?)`, `query(sql, params?)`, `queryOne(sql, params?)`,
  and `close()`;
- primitive parameter arrays for `null`, string, number, and boolean values;
- `query` rows as arrays of plain objects, `queryOne` as one plain object or `null`;
- deterministic closed/stale/invalid-handle failure behavior before provider calls.

Implemented native PostgreSQL API:

- `include/sloppy/data_postgres.h` and `src/data/postgres.c`;
- caller-owned `SlPostgresConnection`, `SlPostgresTransaction`, and `SlPostgresPool`
  wrappers;
- connection-string open/close through libpq;
- `sl_postgres_exec`, `sl_postgres_query`, and `sl_postgres_query_one`;
- transaction begin/commit/rollback plus transaction-scoped exec/query/queryOne helpers;
- a tiny bounded pool with acquire/release and immediate pool-exhausted diagnostics.

Implemented native SQL Server API:

- `include/sloppy/data_sqlserver.h` and `src/data/sqlserver.c`;
- caller-owned `SlSqlServerConnection`, `SlSqlServerTransaction`, and `SlSqlServerPool`
  wrappers;
- ODBC connection-string open/close through the platform driver manager;
- `sl_sqlserver_exec`, `sl_sqlserver_query`, and `sl_sqlserver_query_one`;
- transaction begin/commit/rollback through ODBC autocommit and `SQLEndTran`;
- a tiny bounded pool with acquire/release and immediate pool-exhausted diagnostics;
- connection-string redaction, driver-name extraction, and missing-driver doctor
  diagnostics.

## Ownership/Lifetime Rules

Current fake providers own only JavaScript test/example callbacks and debug event arrays.
Fake transactions close their transaction object after commit/rollback and reject use after
close. Native SQLite, PostgreSQL, and SQL Server connections are caller-owned C wrappers
and must be closed deterministically through `sl_sqlite_close`, `sl_postgres_close`, or
`sl_sqlserver_close`; SQLite prepared statements are finalized on every path, PostgreSQL
`PGresult` values are cleared on every path, and SQL Server ODBC statement/connection/
environment handles are freed on every path. Future JS-visible real connections,
statements, pools, and transactions are resource-table-owned and scoped explicitly.

MAIN1-08 consumes the MAIN1-07 core resource table for SQLite connection handles. JS-facing
data handles wrap only `SlResourceId` values. They never expose provider pointers, ODBC
handles, `sqlite3*`, `sqlite3_stmt*`, `PGconn*`, `PGresult*`, or pointer-like native
addresses to JavaScript. Every SQLite bridge entrypoint validates kind, generation, and
live state before calling provider code. PostgreSQL and SQL Server JS bridges remain
deferred. The V8 engine owns the resource table; provider intrinsic modules may insert,
look up, and close their own resource kinds through that table, but must not create separate
handle registries.

## Invariants

Template query APIs parameterize by default. Lowered query descriptors preserve text and
parameters separately. Native SQLite accepts the existing `?` placeholder lowering path.
Native PostgreSQL accepts the existing `postgres` lowering path with `$1`, `$2`, and so on.
Native SQL Server accepts the existing `question` lowering path with `?` placeholders for
ODBC prepared statements. All implemented native providers bind `null`, text, integer,
float, and boolean values without interpolation.
Provider-specific APIs stay namespaced.

## Diagnostics

Implemented JavaScript errors cover invalid query template usage, fake provider missing
methods, duplicate/missing capability tokens, invalid database capability metadata,
transaction callback misuse, nested transactions, use after closed transaction scope,
bridge-unavailable contexts, SQLite wrapper use after close, and invalid SQLite bridge
parameters.

Native resource lookup diagnostics use `SL_DIAG_RESOURCE_INVALID_ID`,
`SL_DIAG_RESOURCE_STALE_ID`, `SL_DIAG_RESOURCE_WRONG_KIND`, and `SL_DIAG_RESOURCE_CLOSED`.
They report operation and resource kind context without printing native pointer values.

Native SQLite diagnostics use `SL_DIAG_SQLITE_PROVIDER_ERROR` and
`SL_DIAG_DATABASE_UNSUPPORTED_VALUE`. They include provider `sqlite`, operation, SQLite
error text where available, and SQL text without parameter values.

Native PostgreSQL diagnostics use `SL_DIAG_POSTGRES_PROVIDER_ERROR`,
`SL_DIAG_POSTGRES_POOL_EXHAUSTED`, and `SL_DIAG_DATABASE_UNSUPPORTED_VALUE`. They include
provider `postgres`, operation, libpq error text where available, and redacted connection
configuration for open/doctor failures. Passwords and URI credentials must not appear in
diagnostics.

Native SQL Server diagnostics use `SL_DIAG_SQLSERVER_PROVIDER_ERROR`,
`SL_DIAG_SQLSERVER_POOL_EXHAUSTED`, and `SL_DIAG_DATABASE_UNSUPPORTED_VALUE`. They include
provider `sqlserver`, operation, ODBC diagnostic records where available, and redacted
connection configuration for open/doctor failures. Passwords, `PWD`, and access-token
fields must not appear in diagnostics.

`sloppy doctor` can surface deterministic provider readiness metadata through CLI
`doctorChecks`, and it redacts connection-string-like secrets before printing. It does not
run live PostgreSQL or SQL Server checks by default. Future provider doctor CLI work should
reuse the native PostgreSQL and SQL Server doctor helpers behind explicit opt-in flags or
environment gates.

## Tests

`bootstrap.stdlib.data_foundation` executes the ESM stdlib with Node when available and
covers capability metadata, query lowering, fake provider method dispatch, transaction
commit/rollback, rejected async callbacks, nested transaction rejection, use after close,
module/service integration, the honest `data.sqlite.open(...)` bridge-unavailable path,
and SQLite wrapper behavior against a mocked native bridge. This remains Node test
infrastructure only, not a Node compatibility claim.

`data.sqlite.provider` is a native CTest target that links SQLite and covers in-memory
open/close, use after close, exec, parameterized insert, query row shape, queryOne found and
missing behavior, primitive parameter types, unsupported parameter diagnostics, transaction
commit/rollback, nested transaction rejection, transaction use after complete, invalid SQL,
missing table diagnostics, and invalid open diagnostics.

`engine.v8.smoke` adds V8-gated SQLite bridge coverage when the SDK is enabled. Those tests
prove that JavaScript can open `:memory:`, create a table, insert/query data through the
stdlib bridge, close the wrapper, and receive deterministic stale/closed/invalid argument
failures. They are reported separately from default provider tests because default gates do
not enable V8.

`core.capability.registry` covers the runtime capability registry, database read/write
policy, provider mismatch denial, missing/wrong-kind/insufficient capability diagnostics,
filesystem/network skeleton checks, and the bridge-ready deny-before-provider-work
contract.

`data.postgres.provider` is a native CTest target that links libpq and covers redaction,
option validation, use after close, doctor diagnostics, and non-live pool lifecycle
coverage. `data.postgres.live_provider` is a separate opt-in CTest target. When
`SLOPPY_POSTGRES_TEST_URL` is unset CTest marks it skipped; when configured, it connects
with libpq and covers parameterized exec/query/queryOne, transactions, rollback, and tiny
pool acquire/release.

`data.sqlserver.provider` is a native CTest target that uses ODBC when
`SLOPPY_ENABLE_SQLSERVER` is enabled and covers redaction, driver-name extraction,
missing-driver diagnostics, option validation, use after close, unsupported values, and
pool lifecycle behavior. `data.sqlserver.live_provider` is a separate opt-in CTest target.
When `SLOPPY_SQLSERVER_TEST_CONNECTION_STRING` is unset CTest marks it skipped; when
configured, it connects through ODBC and covers parameterized exec/query/queryOne,
transactions, rollback, invalid SQL diagnostics, and tiny pool acquire/release.

CI provider reporting:

- default CI prints whether live provider gates are skipped or enabled before CTest runs;
- SQLite in-memory tests run by default;
- PostgreSQL non-live diagnostics run by default, while live PostgreSQL tests require
  `SLOPPY_POSTGRES_TEST_URL`;
- Windows default CI builds the SQL Server ODBC provider and runs non-live diagnostics;
- Linux/macOS default CI configures `SLOPPY_ENABLE_SQLSERVER=OFF`, reports SQL Server ODBC
  execution as unavailable for those jobs, and verifies the stub/unavailable behavior
  instead of requiring a driver;
- live SQL Server tests require `SLOPPY_SQLSERVER_TEST_CONNECTION_STRING` and an explicit
  driver/server environment. Secrets must not be printed.
- live provider open failures print only a category such as dependency/driver missing,
  service unreachable, credentials rejected, or test failure.

EPIC-20 does not add default database benchmarks. SQLite, PostgreSQL, and SQL Server
benchmarking remains deferred until each benchmark can be clearly labeled as either a
local microbenchmark or an env-gated live benchmark, with secrets redacted and no claims
based on skipped or unavailable services.

## Source Docs

- `docs/data-providers.md`;
- `docs/concurrency.md`;
- `docs/modularity.md`;
- `docs/testing-strategy.md`;
- ADR 0010.

## Open Questions

- Exact row/result JS shape once native stdlib intrinsics exist.
- Exact app-host disposal/resource-table shape for PostgreSQL and SQL Server pool and
  connection handles.
