# Data Module

## Status

Bootstrap data/capabilities foundation implemented. Native SQLite, PostgreSQL, and SQL
Server providers are implemented for C/runtime tests. SQLite has a V8-gated
JavaScript-to-native bridge wired to Plan provider metadata, opaque resource IDs, callback
transactions, and the native database capability hook.
ENGINE-23.A/B defines the native provider/offload descriptor and bounded
per-provider-instance admission model that future SQLite, PostgreSQL, and SQL Server
runtime bridge work must use. ENGINE-23.C implements the serialized SQLite-class blocking
offload model with one worker and one active operation per provider instance, bounded
admission, Slop async completion posting, and cleanup-once behavior. ENGINE-23.D adds the
bounded blocking pool executor for providers that can safely parallelize blocking work.
ENGINE-23.E/F adds cancellation/timeout/shutdown terminal-state handling, late-completion
cleanup-only behavior, and capability-gated dispatch before enqueue/execution. Current
SQLite bridge calls are still synchronous until ENGINE-17 converts them to the executor.
ENGINE-23.G/H adds provider-executor diagnostics/counters, bounded stress smoke, and
`docs/project/provider-runtime-integration-guide.md` for future SQLite/PostgreSQL/SQL
Server bridge work. Current SQLite bridge calls are still synchronous until ENGINE-17
converts them to the executor; PostgreSQL and SQL Server JavaScript bridges remain
deferred.
ENGINE-17.E adds `examples/users-api-sqlite/` plus V8-gated localhost transport evidence
for a small source-built SQLite users API. It proves the current synchronous SQLite bridge
can serve GET/POST JSON handlers over real TCP through `sloppy run --artifacts`; it does
not prove async/offload SQLite, public alpha readiness, PostgreSQL/SQL Server JS bridges,
ORM/migrations, production HTTP edge behavior, or benchmark performance.

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
- `data.sqlite("main")` provider shorthand and `data.sqlite.open(options)`. They validate
  metadata/options and return a safe SQLite connection wrapper when the V8 runtime installs
  `__sloppy.data.sqlite`; otherwise they fail honestly with bridge-unavailable.
  `database` is canonical for explicit open, `path` is only a transitional alias, and
  `path` is accepted only when it is identical to `database`. `capability` is required,
  `access` defaults to `readwrite`, and unsupported fields or mismatched aliases fail
  before bridge work. Provider shorthand keeps that default; read-only capabilities need
  explicit `access: "read"`.
- `data.postgres` provider metadata, `$1` placeholder style, redaction helper, and
  `data.postgres.open(options)` as the future stdlib entry point. It validates options and
  fails honestly until the native bridge exists.
- `data.sqlserver` provider metadata, ODBC `?` placeholder style, redaction and doctor
  helpers, and `data.sqlserver.open(options)` as the future stdlib entry point. It
  validates options and fails honestly until the native bridge exists.
- Plan v1 alpha `dataProviders` and `capabilities` sections describe provider and
  authority metadata for startup validation/audit. The V8 SQLite bridge receives the parsed
  plan and native capability registry from the app host, resolves provider tokens, and
  calls the database check hook before open/read/write work. Direct provider APIs remain
  low-level primitives without capability context.

Implemented native SQLite API:

- `include/sloppy/data_sqlite.h` and `src/data/sqlite.c`;
- caller-owned `SlSqliteConnection` and `SlSqliteTransaction` wrappers;
- `sl_sqlite_open_options_memory`, `sl_sqlite_open`, and `sl_sqlite_close`;
- `sl_sqlite_exec`, `sl_sqlite_query`, and `sl_sqlite_query_one`;
- text/blob interop helpers that copy SQLite transient result storage into caller arenas
  and copy future async/offload parameters into operation-owned arena storage;
- transaction begin/commit/rollback plus transaction-scoped exec/query/queryOne helpers.

Implemented SQLite JS bridge API:

- internal V8 intrinsics under `__sloppy.data.sqlite`;
- V8 layering: `engine_v8.cc` owns the provider-neutral engine core, `intrinsics.cc`
  aggregates provider intrinsic registration, and `intrinsics_sqlite.cc` owns SQLite
  argument conversion, row materialization, resource lookup, cleanup, and provider calls.
  Future PostgreSQL/SQL Server bridges must add sibling intrinsic modules instead of
  expanding `engine_v8.cc`;
- `data.sqlite("main")` and `data.sqlite.open({ database, capability, access })` wrappers
  that store only an opaque `SlResourceId` handle;
- connection methods `exec(sql, params?)`, `query(sql, params?)`, `queryOne(sql, params?)`,
  `transaction(callback)`, and `close()`;
- transaction callback semantics are implemented on the current synchronous SQLite bridge:
  callback success commits, callback throw/reject rolls back, nested transactions are
  rejected, transaction objects expose `exec`, `query`, and `queryOne`, and transaction
  objects reject use after commit/rollback. Async/offloaded SQLite execution through
  ENGINE-23 remains future ENGINE-17 work;
- public prepared statement handles are explicitly deferred. Statement preparation,
  binding, stepping, and finalization remain internal to each provider operation until a
  later task implements JS-visible statement resource lifetime and tests;
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
live state before calling provider code. ENGINE-05 stores capability/provider metadata
beside the native connection resource so later reads, writes, and transaction operations can
re-check the hook.
PostgreSQL and SQL Server JS bridges remain deferred. The V8 engine owns the resource
table; provider intrinsic modules may insert, look up, and close their own resource kinds
through that table, but must not create separate handle registries.

Async/offload ownership:

- provider work must be admitted through a per-provider-instance Slop executor such as
  `sqlite:main` or `sqlite:audit`;
- executor admission requires a provider token and provider-supplied capability check
  hook; database hooks use the Plan-backed capability registry, but the executor itself is
  generic native-provider/offload infrastructure and must not hardcode SQL policy;
- capability denial happens before queue slot reservation, before ownership transfer, and
  before provider worker execution;
- read operations require `read` or `readwrite`, write operations require `write` or
  `readwrite`, and readwrite operations require `readwrite`;
- operation descriptors must copy SQL text, parameter strings/blobs, config values needed
  after submission, capability tokens, operation names, and diagnostic context before they
  leave the caller stack;
- failed admission does not transfer ownership and does not run the operation cleanup
  callback; accepted operations are cleaned exactly once by terminal dispatch, shutdown, or
  dispose;
- active cancellation, timeout, and shutdown post terminal state but do not guarantee that
  a blocking native provider call stops immediately;
- provider-specific SQLite interruption, PostgreSQL cancellation, and SQL Server
  cancellation remain provider-specific future work;
- late worker completion after cancellation, timeout, or shutdown is cleanup-only and must
  not double-settle;
- provider completion posts through `SlAsyncLoop` and can resume JS only through the V8
  owner-thread scheduler;
- SQLite's default async execution mode is implemented as `SERIALIZED_BLOCKING` for a
  single connection unless a future SQLite issue chooses different read/write pool
  semantics;
- PostgreSQL and SQL Server may later use `NONBLOCKING_IO` with native async client APIs or
  `BLOCKING_POOL` when using blocking drivers;
- no provider may use libuv's global threadpool as its runtime policy, create a
  thread-per-request model, bypass capability checks, or invent a separate lifetime model.
- ENGINE-23.C implements the serialized worker lifecycle; future provider bridge work must
  still route through capability-gated dispatch and provider-specific ownership policy
  before claiming scalable SQLite runtime completion.

SQLite text/blob ownership:

- SQLite result column names, text cells, and blob cells are copied into the caller arena
  before statement finalization can invalidate SQLite-owned pointers;
- `sl_sqlite_param_copy_text_to_arena` and `sl_sqlite_param_copy_blob_to_arena` are the
  operation-owned parameter adapters for future async/offloaded provider work;
- synchronous native SQLite calls still bind text/blob parameters with SQLite transient
  ownership during the provider call;
- V8 SQLite string parameters are copied through the private V8 string interop helper and
  then into SQLite operation-owned parameter views before the native provider call;
- no C row/result object and no V8 row object retains a native SQLite transient pointer.

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
bridge-unavailable contexts, SQLite wrapper use after close, invalid SQLite open option
fields, and invalid SQLite bridge parameters.

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
SQLite explicit-open option validation, absent public prepared handles, and SQLite wrapper
behavior against a mocked native bridge. This remains Node test infrastructure only, not a
Node compatibility claim.

`data.sqlite.provider` is a native CTest target that links SQLite and covers in-memory
open/close, use after close, exec, parameterized insert, query row shape, queryOne found and
missing behavior, primitive parameter types including copied blob parameters/results,
unsupported parameter diagnostics, transaction commit/rollback, nested transaction rejection,
transaction use after complete, invalid SQL, missing table diagnostics, text/blob helper
failure behavior, and invalid open diagnostics.

`engine.v8.smoke` adds V8-gated SQLite bridge coverage when the SDK is enabled. Those tests
prove that JavaScript can resolve Plan provider metadata, pass the native capability hook,
open `:memory:`, create a table, insert/query data through the stdlib bridge, close the
wrapper, fail closed when hook metadata is absent, preserve requested read/write/readwrite
handle policy, and receive deterministic stale/closed/invalid argument/capability-denied
failures. They are reported separately from default provider tests because default gates do
not enable V8.

`conformance.users_api_sqlite.localhost_transport` is V8-gated and builds
`examples/users-api-sqlite/app.js` before starting `sloppy run --artifacts` on localhost.
It verifies seeded reads, route lookup, 404, POST JSON insertion, follow-up read visibility,
invalid JSON, denied capability behavior, and server cleanup over real TCP bytes.

`core.provider_executor` is the default native ENGINE-23 provider/offload source. It covers
execution-mode parsing, operation-kind metadata, descriptor helper failure preservation,
invalid descriptor rejection, serialized admission, per-instance capacity isolation,
overflow and recovery, copied input ownership, pre-cancelled and expired-deadline
rejection before enqueue, queued cancellation before execution, active cancellation,
timeout, immediate shutdown, late completion, cleanup exactly once, capability denial
before enqueue, serialized worker FIFO execution, one-active operation behavior, worker
failure diagnostics, completion posting through the libuv async backend, and no ownership
transfer on rejected worker submissions. It also includes bounded stress smoke for many
admitted operations, deterministic overflow, serialized one-active behavior,
blocking-pool worker caps, cleanup once, shutdown safety, and redacted admission
diagnostics. It is not a live database test, benchmark, or SQLite async/offload conversion
claim.

`core.capability.registry` covers the runtime capability registry, database read/write
and readwrite policy, provider mismatch denial, missing/wrong-kind/insufficient capability
diagnostics, filesystem/network skeleton checks, and the bridge-ready deny-before-provider
work contract.

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

- Exact app-host disposal/resource-table shape for PostgreSQL and SQL Server pool and
  connection handles.
