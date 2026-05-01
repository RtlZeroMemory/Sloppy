# Sloppy Data Provider Architecture

## Purpose

This document defines how database providers fit into Sloppy. Database providers are
modules. Sloppy has a common data API, but it does not pretend all SQL dialects are the
same.

## Scope

This document covers:

- common data API;
- SQLite provider phase;
- PostgreSQL provider phase;
- SQL Server provider phase;
- query template lowering;
- provider-specific APIs;
- conceptual native provider interface;
- resource model;
- connection pool lifecycle;
- transaction lifecycle;
- app plan contribution;
- diagnostics;
- distribution implications;
- tests and acceptance criteria.

## Non-Goals

The foundation phase does not implement:

- JavaScript-to-native PostgreSQL or SQL Server resource/intrinsic integration;
- production connection pools;
- SQL parsing;
- provider plugin ABI;
- ORM, migration, or query-builder behavior.
- provider execution runtime implementation outside scoped ENGINE-23 work.

## Current Phase

EPIC-15 added the JavaScript-only data/capabilities foundation: database capability
metadata, query template lowering, a fake data provider contract for tests/examples, and
transaction callback semantics.

EPIC-16 adds the first real provider boundary: native C SQLite support backed by the vcpkg
`sqlite3` package. The native provider can open `:memory:` databases, execute parameterized
statements, query rows, return the first row or no-row for `queryOne`, bind primitive
parameters, and commit or roll back explicit transactions.

MAIN1-08 added the first JavaScript-to-native data bridge for SQLite in V8-enabled runtime
contexts. ENGINE-05 wires that bridge to Plan `dataProviders`, the runtime capability
registry hook, and the public `data.sqlite("main")` provider shorthand. ENGINE-17.A/C
finalizes the public wrapper shape for `open`, `close`, `exec`, `query`, `queryOne`, and
callback-scoped transactions while keeping public prepared statement handles deferred.
ENGINE-17.B/D hardens capability-gated open/use plus result and error policy. The bridge
can open `:memory:` or file SQLite databases described by Plan metadata, execute schema/
data writes, query/queryOne rows as batched plain objects, commit or roll back a
transaction callback, return JSON from handlers, and close resources deterministically.
JavaScript receives only an opaque slot/generation handle object wrapped by the stdlib
facade; it does not receive `sqlite3*`, `sqlite3_stmt*`, or any native pointer.
Non-V8/bootstrap-only contexts still fail honestly with a bridge-unavailable error.

ENGINE-17.E adds the first V8-gated users API runtime proof fixture at
`examples/users-api-sqlite/`. The proof builds a source app with `sloppyc`, emits
SQLite provider/capability metadata, runs `sloppy run --artifacts`, sends real localhost
TCP HTTP requests, dispatches handlers in V8, calls the capability-gated SQLite bridge,
serializes JSON responses, and verifies clean shutdown. The SQLite bridge remains
synchronous in this proof; it is not async/offload, live PostgreSQL, live SQL Server,
benchmark, ORM, migration, production HTTP edge, or public alpha evidence.

EPIC-17 adds the second real provider boundary: native C PostgreSQL support backed by
libpq from the repo-approved vcpkg manifest. The native provider opens connection strings,
executes parameterized `$1` queries, materializes small result sets, supports explicit
transactions, redacts connection strings in diagnostics, and exposes a tiny bounded pool
skeleton. Live PostgreSQL execution is opt-in through `SLOPPY_POSTGRES_TEST_URL`; default
tests do not require a running server.

The JavaScript stdlib exposes `data.postgres` metadata and `data.postgres.open(options)` as
the intended public entry point, but that function fails honestly until the runtime has a
stdlib-to-native intrinsic/resource bridge. No native pointer is exposed to JavaScript and
no fake PostgreSQL success is reported by the stdlib.

EPIC-18 adds the third real provider boundary: native C SQL Server support backed by the
platform ODBC driver manager and Microsoft ODBC Driver for SQL Server on Windows. The native
provider opens ODBC connection strings, executes parameterized `?` queries, materializes
small result sets, supports explicit transactions through ODBC autocommit/SQLEndTran,
redacts connection strings in diagnostics, exposes missing-driver doctor diagnostics, and
provides a tiny bounded pool skeleton. Live SQL Server execution is opt-in through
`SLOPPY_SQLSERVER_TEST_CONNECTION_STRING`; default tests do not require a running SQL
Server instance or installed SQL Server ODBC driver.

The JavaScript stdlib exposes `data.sqlserver` metadata, `data.sqlserver.open(options)`,
redaction, and a small `doctor(options)` shape as the intended public entry point, but
`open` fails honestly until the runtime has a stdlib-to-native intrinsic/resource bridge.
No native pointer is exposed to JavaScript and no fake SQL Server success is reported by the
stdlib.

MAIN1-10 adds the runtime capability registry used by future provider bridge calls.
Database provider access policy is token-based: read operations require `read` or
`readwrite`, write operations require `write` or `readwrite`, and a declared provider token
must match the provider boundary being called. These hooks deny before provider work when
called by a bridge or native host boundary. The existing direct native provider APIs remain
low-level provider primitives and do not carry capability context in their signatures.

FRAMEWORK-01.B makes provider configuration convention-bound. Application configuration
under `Sloppy:Providers:<kind>:<name>` supplies provider defaults, and inline provider
options override those defaults:

```ts
import { Sloppy } from "sloppy";
import { sqlite } from "sloppy/providers/sqlite";

const app = Sloppy.create();
const db = app.use(sqlite("main"));
const testDb = app.use(sqlite("test", { database: ":memory:" }));
```

`app.use(sqlite("main"))` binds `Sloppy:Providers:sqlite:main`. SQLite requires
`database`; missing config fails during compiler/source-input handoff before provider work.
The compiler emits the resolved SQLite database into `dataProviders[]` so the existing V8
SQLite bridge continues to open through Plan metadata. Normal app authors do not write
manual capabilities for this provider path; capability generation remains compiler/Plan
owned.

ENGINE-23.A/B adds the first provider execution runtime foundation: operation descriptors
own queued inputs, per-provider-instance executors enforce bounded admission, accepted
operations transfer cleanup ownership to the executor, overflow leaves caller ownership
intact, and shutdown rejects or cancels deterministically. It still does not run serialized
blocking workers, blocking pools, SQLite async/offload conversion, PostgreSQL/SQL Server
bridges, HTTP backend work, green threads, Node/npm behavior, public alpha docs, or
benchmark claims.

ENGINE-23.C implements `SERIALIZED_BLOCKING` execution for provider-like native operations:
one long-lived worker per provider instance, one active operation at a time, FIFO
activation, bounded admission before ownership transfer, terminal completion posting
through the Slop async runtime, and cleanup-once behavior on success, failure, overflow,
shutdown, dispose, and late completion. This is the default runtime model for future
SQLite-class single-connection provider execution, but the real SQLite JS bridge still
uses its existing synchronous provider path until ENGINE-17 routes it through the executor.
ENGINE-23.D implements `BLOCKING_POOL` execution for provider-like native operations:
bounded long-lived workers per provider instance, bounded queue and in-flight counts,
deterministic overflow before ownership transfer, cleanup-once success/failure/shutdown
behavior, and libuv completion posting. ENGINE-23.E/F adds terminal-state and
capability-gated dispatch semantics to those executor modes: capability checks happen
before queue slot reservation through a provider-supplied policy hook, pre-cancelled and
expired-deadline work rejects before enqueue, queued cancellation prevents execution when
possible, active cancellation/timeout/shutdown posts terminal state without claiming
native interruption, and late worker results are cleanup-only. The executor model is
generic Slop-owned native-provider/offload infrastructure, not SQL-only plumbing; future
native providers/addons with blocking calls should use it rather than libuv's shared
threadpool. ENGINE-23.G/H adds redacted diagnostics/counters, bounded stress smoke, and
the integration guide in `docs/project/provider-runtime-integration-guide.md`. SQLite
remains `SERIALIZED_BLOCKING` by default unless ENGINE-17 later changes that provider
policy. PostgreSQL/SQL Server JavaScript bridges remain deferred.

## Provider Support Classification

| Provider | Native status | Default validation | Live validation | JS bridge status |
| --- | --- | --- | --- | --- |
| SQLite | Native C provider exists and is linked through the default build. V8-enabled runtime contexts install a JS bridge for open/exec/query/queryOne/transaction/close. | In-memory open/query/exec/transaction, resource-table, capability hook, and stdlib wrapper tests run in default CTest/Node gates. | No external service is required for current provider tests. V8 execution and the users API localhost proof are separate optional gates. | Implemented for V8-enabled SQLite only. The bridge requires Plan provider metadata and a capability registry, calls the database capability hook before open/read/write/transaction provider work, records resource access mode, and fails closed if the hook inputs are absent. |
| PostgreSQL | Native libpq provider boundary exists. | Non-live option, doctor, redaction, use-after-close, and lifecycle tests run in default CTest. | Opt-in `data.postgres.live_provider` requires `SLOPPY_POSTGRES_TEST_URL`; when unset, CTest reports it skipped. | Deferred; `data.postgres.open(...)` validates/redacts and then fails with bridge-unavailable. |
| SQL Server | Native ODBC provider boundary exists when `SLOPPY_ENABLE_SQLSERVER` is enabled; otherwise stubs report unavailable. | Windows default tests cover ODBC-enabled non-live diagnostics; Linux/macOS defaults cover unavailable/stub behavior. | Opt-in `data.sqlserver.live_provider` requires `SLOPPY_SQLSERVER_TEST_CONNECTION_STRING`, an ODBC driver, and a reachable SQL Server; when unset, CTest reports it skipped. | Deferred; `data.sqlserver.open(...)` validates/redacts and then fails with bridge-unavailable. |

Default CI proves only default/non-live provider behavior. It does not prove live
PostgreSQL, live SQL Server, SQL Server driver installation, package-smoke provider
availability, or JavaScript-to-native provider execution.

## Provider Execution Policy

Canonical ENGINE-23 architecture:
`docs/project/provider-execution-runtime-architecture.md`.

Sloppy uses libuv internally for eventing, timers, wakeups, and async completion plumbing.
libuv's global threadpool is not Sloppy's provider runtime. Provider work must be admitted
through Sloppy-owned provider executors, complete through `SlAsyncCompletion`, and resume
JavaScript only on the V8 owner thread.

Execution modes:

| Mode | Semantics |
| --- | --- |
| `INLINE_FAST` | Bounded metadata/config work only. It must not block on I/O, database calls, disk, network, or contended locks. |
| `SERIALIZED_BLOCKING` | Implemented serialized offload: one long-lived worker and one active operation at a time for one provider instance. This is the default for a single SQLite connection unless a later SQLite task changes the policy. |
| `BLOCKING_POOL` | Implemented bounded worker pool for one provider instance when the provider is safe to parallelize blocking calls. Worker count, queue capacity, and in-flight count are fixed by provider instance configuration, and the pool does not create one thread per request. |
| `NONBLOCKING_IO` | True async provider/client path through socket readiness or provider async APIs. No worker is occupied while waiting. |
| `EXTERNAL_MANAGED` | Future escape hatch for external runtimes/pools. It still must use Sloppy admission, completion, diagnostics, cancellation, and lifetime rules. |

Provider instances are per logical configured resource: `sqlite:main`, `sqlite:audit`,
future `postgres:main`, or future `sqlserver:reporting`. Each instance has its own queue
capacity, in-flight count, shutdown state, optional worker count, and counters. There is
no unbounded global provider queue, no thread-per-request behavior, and no
provider-specific async model outside Slop's admission/completion contract.

ENGINE-23.A/B turns these modes into implementation-grade descriptor and admission
contracts. ENGINE-23.C implements the serialized blocking worker model for provider-like
native operations, ENGINE-23.D implements the bounded blocking pool worker model for
provider-like native operations, ENGINE-23.E/F implements generic cancellation, timeout,
shutdown, late-completion, and capability-gated dispatch behavior, and ENGINE-23.G/H adds
redacted diagnostics/counters, bounded stress smoke, and SQLite/PostgreSQL/SQL Server
integration guidance. Real SQLite bridge conversion remains ENGINE-17.

Provider operation descriptors must own or retain all memory needed after submission: SQL
strings, parameter text/blob values, provider config references, capability token,
diagnostic context, and cleanup payloads. Borrowed request-arena views cannot be queued
unless the owning request/app scope is retained safely. Cleanup runs exactly once. Late
provider completion after cancellation, timeout, or shutdown is cleanup-only and must not
double-settle. Cancellation has two layers: Slop cancellation/deadline terminal state, and
optional provider-specific interruption later. SQLite interruption, PostgreSQL
cancellation, and SQL Server cancellation remain provider-specific follow-ups.

Executor dispatch is capability-gated. Each provider executor is initialized with the
provider token plus a required capability-check hook. The database hook uses the
Plan-backed capability registry for configured instances such as `data.main`; future
non-database providers supply their own hook while preserving the same denial-before-
enqueue contract. Submitted operations must carry a required capability token and access
operation. Read requires `read` or `readwrite`, write requires `write` or `readwrite`, and
readwrite requires `readwrite`. Missing capability, wrong capability kind, insufficient
access, provider-token mismatch, and provider-kind mismatch all reject before enqueue and
before ownership transfer. Denial diagnostics use safe tokens, operation names, provider
instance/kind context, and registry metadata; they do not print raw native pointers,
connection strings, SQL parameters, or secret config values.

Overflow is normal runtime backpressure. A full provider-instance queue rejects admission
with `SL_STATUS_CAPACITY_EXCEEDED`; the executor does not take ownership and the caller can
map that to an HTTP overload/error response. Shutdown stops new admission and the current
native executor uses immediate terminal cancellation for pending and active provider work.
Already-running blocking provider callbacks are not forcibly interrupted; they finish
later and their completion is treated as late cleanup-only behavior. Future drain periods
or provider-specific interruption hooks must preserve cleanup-once and safe late-completion
behavior.

## Future Phase Order

1. SQLite first, built-in/static provider. Native C provider implemented; JavaScript bridge
   implemented for V8-enabled runtime contexts in MAIN1-08.
2. PostgreSQL second, via libpq. Native C provider implemented; JavaScript bridge
   deferred.
3. SQL Server third, via Microsoft ODBC Driver and ODBC API on Windows. Native C provider
   implemented; JavaScript bridge deferred.
4. Dynamic provider ABI later.

## File And Module Layout

Likely future layout:

```text
src/data/common/
src/data/sqlite/
src/data/postgres/
src/data/sqlserver/
include/sloppy/data.h
tests/golden/data/
tests/integration/data/
```

SQLite now owns `src/data/sqlite.c`. PostgreSQL now owns `src/data/postgres.c`. SQL Server
now owns `src/data/sqlserver.c`. Do not add a generic provider framework directory before
its phase.

## Internal Architecture

The JS API sends query templates to a Sloppy data service. The runtime validates resource
IDs, permissions, and provider configuration. Provider implementations own SQL lowering,
binding, execution, and result conversion behind a common interface.

## Public API Shape

SQLite stdlib shape:

```ts
import { Sloppy, data } from "sloppy";

const SqliteModule = Sloppy.module("data.sqlite")
  .capabilities(caps => {
    caps.addDatabase("data.main", {
      provider: "sqlite",
      database: ":memory:",
      access: "readwrite",
    });
  })
  .services(services => {
    services.addSingleton("data.main", () => data.sqlite.open({
      database: ":memory:",
      capability: "data.main",
      access: "readwrite",
    }));
  })
```

V8 artifact/runtime handler shape:

```js
app.mapGet("/users", async ctx => {
  const db = data.sqlite("main");
  try {
    await db.exec("create table users (id integer primary key, name text not null)");
    await db.exec("insert into users (name) values (?)", ["Ada"]);
    return Results.json(await db.query("select id, name from users", []));
  } finally {
    await db.close();
  }
});
```

Native C test shape:

```c
SlSqliteConnection db = {0};
SlSqliteOpenOptions options = sl_sqlite_open_options_memory();
sl_sqlite_open(arena, &options, &db, diag);
sl_sqlite_exec(arena, &db, sql, params, param_count, &exec_result, diag);
sl_sqlite_query(arena, &db, sql, params, param_count, NULL, &rows, diag);
sl_sqlite_close(&db);
```

PostgreSQL stdlib shape:

```ts
import { Sloppy, data } from "sloppy";

const PostgresModule = Sloppy.module("data.postgres")
  .capabilities(caps => {
    caps.addDatabase("data.main", {
      provider: "postgres",
      configKey: "SLOPPY_POSTGRES_TEST_URL",
      access: "readwrite",
    });
  })
  .services(services => {
    services.addSingleton("data.main", () => data.postgres.open({
      connectionString: "postgres://localhost/sloppy_test",
      maxConnections: 2,
    }));
  });
```

Native C test shape:

```c
SlPostgresConnection db = {0};
SlPostgresOpenOptions options =
    sl_postgres_open_options_connection_string(connection_string);
sl_postgres_open(arena, &options, &db, diag);
sl_postgres_exec(arena, &db, sql, params, param_count, &exec_result, diag);
sl_postgres_query(arena, &db, sql, params, param_count, NULL, &rows, diag);
sl_postgres_close(&db);
```

SQL Server stdlib shape:

```ts
import { Sloppy, data } from "sloppy";

const SqlServerModule = Sloppy.module("data.sqlserver")
  .capabilities(caps => {
    caps.addDatabase("data.main", {
      provider: "sqlserver",
      configKey: "SLOPPY_SQLSERVER_TEST_CONNECTION_STRING",
      access: "readwrite",
    });
  })
  .services(services => {
    services.addSingleton("data.main", () => data.sqlserver.open({
      connectionString:
        "Driver={ODBC Driver 18 for SQL Server};Server=localhost;Database=sloppy_test;Trusted_Connection=yes;TrustServerCertificate=yes;",
      maxConnections: 2,
    }));
  });
```

Native C test shape:

```c
SlSqlServerConnection db = {0};
SlSqlServerOpenOptions options =
    sl_sqlserver_open_options_connection_string(connection_string);
sl_sqlserver_open(arena, &options, &db, diag);
sl_sqlserver_exec(arena, &db, sql, params, param_count, &exec_result, diag);
sl_sqlserver_query(arena, &db, sql, params, param_count, NULL, &rows, diag);
sl_sqlserver_close(&db);
```

Route usage:

```ts
app.mapGet("/users/{id:int}", async ({ services, route }) => {
  const db = services.get("data.main");

  const user = await db.queryOne`
    select id, name, email
    from users
    where id = ${route.id}
  `;

  return user ? Results.ok(user) : Results.notFound();
});
```

## Common Data API

Common operations:

- `query`: returns zero or more rows;
- `queryOne`: returns one row or null/undefined according to final JS convention;
- `exec`: executes a statement where rows are not expected;
- `transaction`: runs an async callback in a transaction scope;
- `prepare`: future explicit statement preparation;
- `stream`: future row streaming for large results;
- `close`/`dispose`: explicit cleanup where appropriate;
- health check: provider-defined readiness check.

Common API rules:

- common methods must work across SQLite, PostgreSQL, and SQL Server where dialect allows;
- common API must not hide provider-specific SQL dialect differences;
- unsupported parameter types produce diagnostics before unsafe coercion;
- result shape must be stable enough for plan/tooling docs before release.

Current bootstrap behavior:

- `sql` lowers tagged templates to frozen descriptors with `text`, `parameters`,
  `parameterCount`, `placeholderStyle`, and `placeholders`.
- `data.lowerQueryTemplate(strings, values, { placeholderStyle })` supports `question`,
  `postgres`, and `named`.
- `data.createFakeProvider(...)` creates a JS-only provider for tests/examples.
- fake providers expose `query`, `queryOne`, `exec`, and `transaction`.
- fake provider methods accept tagged templates or already-lowered query objects.
- fake transactions commit when the callback resolves, roll back when it throws/rejects,
  reject nested transactions, and reject use after close.
- the fake provider never opens a database and never executes SQL.
- `data.sqlite("main")` resolves the Plan provider token `data.main` and opens a native
  SQLite connection through the resource-table bridge in V8-enabled runtime contexts. The
  shorthand keeps the normal `readwrite` default; a read-only capability must use explicit
  `data.sqlite.open({ database, capability, access: "read" })`.
- `data.sqlite.open({ database, capability, access })` is the explicit low-level SQLite
  open shape. The older `path` key is accepted as a transitional alias for `database`; new
  docs and fixtures should use `database`. If both `database` and `path` are supplied,
  they must match exactly or validation fails before bridge work begins. `database` is
  canonical, `capability` is required for explicit opens, `access` defaults to
  `readwrite`, `readwrite` opens require readwrite authority, and unsupported option fields
  fail before bridge work begins.
- In bootstrap-only or non-V8 contexts, SQLite open reports that the bridge is
  unavailable.
- `data.postgres.open(options)` validates PostgreSQL connection string options, redacts
  credentials, and then reports that the native stdlib bridge is unavailable.
- `data.sqlserver.open(options)` validates SQL Server ODBC connection string options,
  redacts `PWD`, `Password`, and access tokens, and then reports that the native stdlib
  bridge is unavailable.
- `data.sqlserver.doctor(options)` returns a small bridge-unavailable doctor object in
  JavaScript; native C tests cover real ODBC driver-manager/driver detection.
- native SQLite provider tests use the same `?` placeholder lowering contract at the C
  boundary.
- native SQLite result text/blob values are copied into caller arenas before SQLite
  statement finalization, and helper functions exist for future operation-owned text/blob
  parameters before async/offloaded submission.
- native PostgreSQL provider tests use the same `$1`, `$2`, ... placeholder lowering
  contract at the C boundary.
- native SQL Server provider tests use the same `?` placeholder lowering contract at the C
  boundary.

Native SQLite behavior:

- supported path for tests: `:memory:`;
- native file paths can be passed to SQLite, but public file database capability policy is
  still deferred;
- `exec` returns SQLite `changes`;
- `query` returns arena-owned rows with stable column names and values;
- `queryOne` returns the first row or `found = false`;
- parameters support null, text, blob, integer, float, and boolean as 0/1;
- text and blob results are arena-owned copies, not SQLite transient statement pointers;
- empty result sets preserve column metadata with `row_count = 0`;
- duplicate native column names are preserved in column order; JS row objects use ordinary
  property assignment, so the later duplicate column value overwrites the earlier value;
- unsupported parameter kinds fail before unsafe coercion;
- nested transactions are rejected for now;
- public prepared statement handles are deferred. `sqlite3_prepare_v2`, binding, stepping,
  and finalization stay internal to `exec`, `query`, `queryOne`, and transaction-scoped
  operations until a later task can implement statement resource lifetime, capability
  checks, stale-handle diagnostics, and cleanup tests;
- statements are finalized on all paths and close is deterministic.

SQLite JS bridge behavior:

- internal V8 intrinsic namespace: `__sloppy.data.sqlite.open/exec/query/queryOne`,
  transaction begin/commit/rollback and transaction-scoped exec/query/queryOne helpers,
  plus `close`;
- V8 engine layering: `engine_v8.cc` installs the private `__sloppy.data` namespace and
  stays provider-neutral. `src/engine/v8/intrinsics.cc` aggregates provider bridge
  registration, while `src/engine/v8/intrinsics_sqlite.cc` owns SQLite-specific argument
  validation, parameter conversion, resource lookup, cleanup, and native provider calls.
  Future PostgreSQL and SQL Server JS bridges must add their own intrinsic modules instead
  of adding provider logic to `engine_v8.cc`;
- public stdlib facade: `data.sqlite("main")` or
  `data.sqlite.open({ database, capability, access })` returns a frozen connection wrapper;
- supported operations: `exec(sql, params?)`, `query(sql, params?)`, `queryOne(sql, params?)`,
  `transaction(callback)`, and `close()`;
- `transaction(callback)` begins a native SQLite transaction, passes a transaction object
  with `exec`, `query`, and `queryOne`, commits when the callback resolves, and rolls back
  when the callback throws or rejects. Nested transactions are rejected. Transaction objects
  reject use after commit or rollback. This is still the synchronous bridge/native provider
  path; async provider offload through ENGINE-23 remains a later ENGINE-17 slice;
- no public `prepare` API is exposed. Prepared statements remain internal provider
  implementation details for each operation;
- `query` returns arrays of plain objects keyed by deterministic column names;
- `queryOne` returns one plain object or `null`;
- SQLite `NULL` maps to JavaScript `null`; integer and float values map to JavaScript
  numbers; text maps to strings; blob values map to V8-owned `Uint8Array` instances when a
  bridge path materializes them;
- duplicate column names follow JavaScript object semantics: the last value assigned for a
  repeated name wins. Authors should alias duplicate projections explicitly;
- parameters are positional arrays containing only `null`, string, number, or boolean
  values. Boolean parameters bind as SQLite integers `0` or `1`. String parameters are
  copied into the bridge operation arena before the native provider call so future async/
  offload work cannot depend on V8 transient storage. JavaScript parameter arrays are
  capped at 32,766 elements before the bridge reserves native parameter storage; larger
  arrays fail with a stable redacted parameter-count diagnostic before provider work;
- wrapper double close is deterministic and idempotent; query/exec/queryOne/transaction
  after close fails before entering native code;
- stale, closed, invalid, and wrong-kind resource IDs fail through the core resource-table
  diagnostics before provider code runs;
- Plan provider lookup resolves `data.sqlite("main")` to `data.main`, verifies provider
  kind `sqlite`, and requires database metadata. Provider-token shorthand keeps the
  requested/default access and does not silently downgrade readwrite opens to read;
- open, exec, query, queryOne, and transaction operations call the native database
  capability hook before provider work. Read operations require `read` or `readwrite`,
  write operations require `write` or `readwrite`, and readwrite opens require
  `readwrite`. A handle opened with `read` denies writes; a handle opened with `write`
  denies queries before provider work even if the capability token has broader access. If
  the engine lacks Plan/capability hook inputs, the bridge fails closed instead of opening
  an unchecked provider;
- engine destruction disposes the resource table and closes leaked live SQLite resources.

Capability status:

- ENGINE-05 does not implement a new permission engine. It calls the existing MAIN1-10
  database capability hook with provider token, capability token, and read/write operation
  before SQLite provider work begins;
- denied checks return redacted permission diagnostics. They do not include SQL parameter
  values, connection strings, resource pointer values, or native pointers. Native SQLite
  provider errors may include SQL text and SQLite error text, but parameter values are kept
  out of diagnostics. SQL text is therefore not a secret-bearing channel; callers must not
  put secrets directly in SQL literals;
- filesystem and network capabilities remain metadata/check-only skeletons.

Native PostgreSQL behavior:

- supported connection path: connection string;
- `PQconnectdb`, `PQstatus`, `PQerrorMessage`, `PQfinish`, and `PQexecParams` are used
  behind the provider boundary;
- `exec` returns affected row count when libpq command status reports one;
- `query` returns arena-owned rows with stable column names and values;
- `queryOne` returns the first row or `found = false`;
- parameters support null, text, integer, float, and boolean without SQL interpolation;
- `$1`, `$2`, ... placeholders are the provider's query-template style;
- unsupported parameter kinds fail before unsafe coercion;
- nested transactions are rejected for now;
- `BEGIN`, `COMMIT`, and `ROLLBACK` implement transactions;
- pool support is intentionally tiny: bounded max connection count, acquire/release,
  close-all, no waiting queue, no health checks, no background threads, no idle pruning,
  and no thread-safety contract;
- pool close is idempotent after the first successful close, while releasing an idle,
  closed, foreign, or transaction-active connection is rejected;
- `PGresult` values are cleared on every path and connections close deterministically.

Native SQL Server behavior:

- supported connection path: ODBC connection string;
- ODBC support is optional/gated by `SLOPPY_ENABLE_SQLSERVER`, defaulting on for Windows and
  off elsewhere;
- `SQLAllocHandle`, `SQLDriverConnect`, `SQLPrepare`, `SQLBindParameter`, `SQLExecute`,
  `SQLFetch`, `SQLGetData`, `SQLGetDiagRec`, `SQLEndTran`, `SQLDisconnect`, and
  `SQLFreeHandle` are used behind the provider boundary;
- `exec` returns affected row count when ODBC reports one;
- `query` returns arena-owned rows with stable column names and values;
- `queryOne` returns the first row or `found = false`;
- parameters support null, text, integer, float, and boolean without SQL interpolation;
- `?` placeholders are the provider's query-template style;
- unsupported parameter kinds fail before unsafe coercion once a statement can be prepared;
- nested transactions are rejected for now;
- transactions disable autocommit, commit/rollback through `SQLEndTran`, and restore
  autocommit before the transaction handle is closed;
- pool support is intentionally tiny: bounded max connection count, acquire/release,
  close-all, no waiting queue, no health checks, no background threads, no idle pruning,
  and no thread-safety contract;
- pool close is idempotent after the first successful close, while releasing an idle,
  closed, foreign, or transaction-active connection is rejected;
- ODBC statement, connection, and environment handles close deterministically.

Transaction example:

```ts
await db.transaction(async tx => {
  await tx.exec`
    insert into users (name, email)
    values (${name}, ${email})
  `;

  await tx.exec`
    update audit_log
    set touched = true
    where user_email = ${email}
  `;
});
```

## Query Template Lowering

Template literals parameterize by default.

Input:

```ts
await db.query`
  select id, name
  from users
  where id = ${id}
`;
```

Internal representation:

```text
SqlTemplate:
  segments
  params
```

PostgreSQL lowering:

```sql
select id, name
from users
where id = $1
```

SQLite/SQL Server placeholder style:

```sql
select id, name
from users
where id = ?
```

Providers own placeholder numbering, parameter conversion, and unsupported value
diagnostics.

Placeholder formats:

- PostgreSQL: `$1`, `$2`, `$3`;
- SQLite: `?` in positional order for the common lowering path;
- SQL Server/ODBC: `?` in positional order for the common lowering path.

Raw SQL may exist later only under an explicitly unsafe name, such as `db.rawUnsafe(...)`.

The bootstrap lowering descriptor is:

```js
{
  __sloppyQuery: true,
  text: "select id from users where id = ?",
  parameters: [id],
  parameterCount: 1,
  placeholderStyle: "question",
  placeholders: [{ index: 0, text: "?", name: null, position: 1 }]
}
```

Values are never interpolated into `text` by the blessed template path.

## Provider-Specific APIs

Provider-specific APIs live under provider namespaces.

Examples:

- `postgres.listen`;
- `postgres.notify`;
- `postgres.copyFrom`;
- `postgres.jsonb`;
- `sqlserver.bulkCopy`;
- `sqlserver.tableValuedParameter`;
- `sqlite.pragma`;
- `sqlite.backup`.

Provider-specific APIs must not be presented as portable.

## Conceptual Native Provider Interface

Not final ABI:

```c
typedef struct SlDbProviderV1 {
    SlStr name;

    SlStatus (*open)(
        SlRuntime* runtime,
        const SlDbOpenOptions* options,
        SlResourceId* out_connection
    );

    SlStatus (*close)(
        SlRuntime* runtime,
        SlResourceId connection
    );

    SlStatus (*prepare)(
        SlRuntime* runtime,
        SlResourceId connection,
        SlStr sql,
        SlResourceId* out_statement
    );

    SlStatus (*bind)(
        SlRuntime* runtime,
        SlResourceId statement,
        const SlDbParam* params,
        size_t param_count
    );

    SlStatus (*execute)(
        SlRuntime* runtime,
        SlResourceId statement,
        SlDbResult* out_result
    );

    SlStatus (*begin_tx)(...);
    SlStatus (*commit_tx)(...);
    SlStatus (*rollback_tx)(...);
} SlDbProviderV1;
```

The real ABI must be Sloppy-owned, versioned, engine-independent, and integrated with
resource tables and permissions.

## Resource Model

Resources:

- pool resource;
- connection resource;
- statement resource;
- transaction scope/resource.

All future JS-visible provider resources use the Sloppy resource table with generation
counters. JS never receives raw C pointers. The implemented MAIN1-07 table provides
`SlResourceId { slot, generation }`, kind validation, stale-handle protection, wrong-kind
diagnostics, close/reuse behavior, table exhaustion behavior, and deterministic cleanup of
remaining live entries on table dispose.

Resource rules:

- pool resources are app-lifetime singleton-style resources;
- connection resources are checked out from a pool and returned deterministically;
- statement resources are closed when execution completes unless explicitly prepared;
- transaction resources pin a connection until commit/rollback;
- debug builds report leaked statements, connections, and transactions at request or app
  shutdown.

Bridge rules:

- `data.sqlite.open(...)` and later provider bridge calls may expose only opaque JS handle
  objects wrapping `SlResourceId`;
- provider kind is validated by native table lookup, not by fields supplied by JS;
- stale, closed, missing-slot, and wrong-kind IDs fail before provider code runs;
- diagnostics must include operation/kind context where useful and must not include native
  addresses;
- SQLite MAIN1-08 bridge work depends on this table and reuses it directly through
  `src/engine/v8/intrinsics_sqlite.cc`;
- provider-specific V8 bridge modules may consume the engine-owned table, but must not
  create ad hoc handle maps and must not place provider lookup/conversion code in
  `engine_v8.cc`.

ENGINE-17.E adds the users API runtime proof for SQLite. PostgreSQL and SQL Server
JavaScript bridges remain deferred and must follow the same provider executor and
resource-table rules when scoped.

ENGINE-19.D registers the existing SQLite/capability proof under matrix-visible CTest
names: `conformance.sqlite.native_provider`,
`conformance.capability.native_registry`, `conformance.capability.provider_executor`,
V8-gated `conformance.sqlite.bridge` and `conformance.sqlite.denied_capability`, and
V8-gated localhost `conformance.users_api_sqlite.localhost_transport`. These are evidence
names for implemented behavior, not new PostgreSQL/SQL Server bridge, async SQLite
offload, live-provider, package, benchmark, public alpha, or production-edge HTTP claims.

## Connection Pool Lifecycle

Target lifecycle:

1. provider module registers app-level pool as singleton resource;
2. runtime validates provider config at startup;
3. request lazily checks out connection;
4. query returns connection to pool when done;
5. app shutdown drains pool;
6. debug builds report leaked connections/statements.

Pool sizing:

- default sizing is provider-defined and conservative;
- min/max settings appear in module config;
- invalid pool sizes produce startup diagnostics;
- pool implementation must not be introduced before provider phase tests.

## Transaction Lifecycle

Target lifecycle:

1. begin transaction;
2. pin one connection;
3. execute operations;
4. commit on success;
5. rollback on error or rejected promise;
6. release statements;
7. return connection to pool.

Transaction scope must remain alive until async callback settles.

If a transaction callback throws or returns a rejected promise, the transaction rolls back
unless it was already explicitly completed. Double commit, double rollback, and use after
rollback are diagnostics.

## App Plan Contribution

Hard constraint for future framework/compiler work: normal app authors should not
hand-write the provider capability metadata shown here. Provider module declarations and
`sloppyc` plan emission must generate default provider/capability entries for ordinary
CRUD apps. Raw Plan capability blocks remain useful as the runtime representation, audit
output, fixture format, and advanced escape hatch, but they are not the target TypeScript
developer experience.

Post-Core framework target:

```ts
import { sqlite } from "sloppy/providers/sqlite";

const db = app.use(sqlite("main"));
```

The compiler should infer read/write/readwrite capability needs from recognized provider
calls (`query`, `queryOne`, `exec`, and `transaction`) when it can do so safely. Provider
configuration is convention-bound by provider kind/name under keys such as
`Sloppy:Providers:sqlite:main:*`. If capability inference is uncertain, the compiler must
fail with a helpful diagnostic or require explicit metadata; it must not silently emit
unsound capability metadata.

Providers contribute:

```json
{
  "dataProviders": [
    {
      "token": "data.main",
      "provider": "postgres",
      "lifetime": "singleton-pool",
      "config": {
        "connectionStringKey": "DATABASE_URL"
      }
    }
  ],
  "permissions": {
    "database": [
      {
        "provider": "postgres",
        "token": "data.main"
      }
    ]
  }
}
```

Plans reference config keys, not secret values.

## Diagnostics

Diagnostics must be actionable and redact secrets.

Provider diagnostics may name the provider, operation, driver/dependency category, and
configuration key. They must not print full secret-bearing connection strings, passwords,
access tokens, or environment variable values. PostgreSQL and SQL Server keep
provider-specific connection-string redaction in their diagnostic paths and should use the
shared diagnostic redaction helper only as an additional guard for generic text.

SQL Server missing driver:

```text
sloppy: SQL Server provider unavailable

  Provider:
    sloppy:data/sqlserver

  Reason:
    Microsoft ODBC Driver for SQL Server was not found.

  Install:
    Microsoft ODBC Driver 18 for SQL Server

  Then run:
    sloppy doctor
```

SQLite provider error:

```text
error[SLOPPY_E_SQLITE_PROVIDER]: sqlite provider prepare failed

  Provider:
    sqlite

  Operation:
    query

  SQLite:
    no such table: users

  SQL:
    select id from users
```

Parameter binding diagnostic:

```text
error[SLOPPY_E_DATABASE_UNSUPPORTED_VALUE]: unsupported sqlite parameter value

  Provider:
    sqlite

  Operation:
    exec
```

## Distribution Implications

SQLite is consumed through the repo-approved vcpkg manifest as `sqlite3` and linked through
the vcpkg CMake target. PostgreSQL requires libpq DLL strategy. SQL Server uses system ODBC
headers/libraries discovered by CMake and depends on Microsoft ODBC Driver presence for live
use. `sloppy doctor` should later surface the same missing-driver and incomplete-config
checks currently covered by the native SQL Server doctor helper.

Package smoke proves packaged CLIs start and stdlib assets/manifest fields are present. It
does not prove live PostgreSQL, live SQL Server, SQL Server ODBC driver installation,
credentials, V8 execution, or JS-to-native provider bridges.

## Security Rules

- template query APIs parameterize by default;
- raw SQL escape hatch must be explicitly named, such as `rawUnsafe`;
- connection strings are secrets;
- diagnostics redact secrets;
- provider resources use generation-checked IDs.

## Concurrency and Async Provider Strategy

The public DB API is always async and promise-friendly. Provider implementations choose the
best native strategy behind that API, and all completions post back to the owning JS event
loop described in `docs/concurrency.md`.

TASK 09.C provides only an inline/fake `SlWorkerPool` skeleton that proves the completion
contract. It is not a database execution backend and must not be used to run blocking
SQLite, libpq, ODBC, or filesystem work.

ENGINE-23.C/D are now the first real provider worker runtimes:
`SERIALIZED_BLOCKING` work runs on a Slop-owned per-provider-instance worker, and
`BLOCKING_POOL` work runs on a bounded set of Slop-owned per-provider-instance workers for
providers that can safely parallelize blocking calls. Both modes post completion through
the thread-safe `SlAsyncLoop` backend. Provider workers must never enter V8. This is
generic native-provider/offload infrastructure, not a SQL-only executor; future native
providers/addons with blocking calls should use these Slop-owned workers instead of
libuv's shared threadpool. The SQLite bridge has not yet been converted to this executor,
SQLite remains serialized by default, and PostgreSQL/SQL Server bridge work remains
deferred. ENGINE-23.G/H evidence is bounded stress/smoke coverage only; it proves executor
admission, worker caps, cleanup, and terminal-state behavior, not database throughput or
public performance.

Likely first strategies:

- SQLite uses a dedicated DB executor or worker-pool strategy first.
- PostgreSQL/libpq may begin with blocking worker-pool calls or later use nonblocking socket
  integration.
- SQL Server/ODBC likely uses a worker-pool strategy first.

Transaction scope lives until the async callback settles. A thrown or rejected callback
rolls back unless the transaction helper has already committed by policy. Providers must
support cancellation/deadline where possible, or document unsupported cancellation in
diagnostics and tests.

## Testing Requirements

Provider tests must include:

- parameter binding;
- placeholder lowering;
- transaction commit/rollback;
- statement cleanup;
- connection pool lifecycle;
- request-scope disposal;
- provider diagnostics;
- driver-unavailable skip/fail behavior;
- provider-specific API tests.

Integration tests that require external services must be gated behind environment variables
or future test containers. Skipped tests must explain which variable or driver is missing.
Live provider tests are registered separately from default provider tests and use CTest skip
code `77` when not configured. When configured but failing to open, they report only a
redacted category such as dependency/driver missing (where applicable), service
unreachable, credentials rejected, or test failure.

## Quality Gates

- provider plan fixtures are covered by golden tests;
- SQL template lowering tests pass for each provider;
- integration tests skip with a clear reason when driver/config is unavailable;
- diagnostics redact connection strings;
- resource leak tests pass in debug builds;
- no provider dependency is added outside its implementation phase.

## Implementation Phases

### Phase A: Common Data Abstractions

Tasks:

- define query template representation;
- define common JS API shape;
- define native conceptual provider interface;
- define data provider plan entries;
- add golden fixtures.

Acceptance:

- no provider dependency added;
- placeholder lowering tests can run without a database;
- plan fixture contains `dataProviders` and permissions.

### Phase B: SQLite Provider

Tasks:

- add sqlite dependency intentionally;
- implement static provider module;
- open database file through capability/path rules;
- implement query/exec/queryOne;
- implement transactions;
- add cleanup diagnostics.

Acceptance:

- SQLite route fixture can query local test DB;
- transaction commit/rollback tests pass;
- leaked statement diagnostics work in debug.

### Phase C: PostgreSQL Provider

Tasks:

- add libpq dependency intentionally;
- implement driver/library discovery;
- implement connection pool;
- lower placeholders to `$n`;
- add env-gated integration tests;
- define packaging/DLL strategy.

Acceptance:

- missing libpq/config diagnostic is tested;
- query/transaction tests pass when environment is configured;
- connection string is redacted.

### Phase D: SQL Server Provider

Implemented:

- ODBC integration is isolated in `src/data/sqlserver.c`;
- Windows builds enable `SLOPPY_ENABLE_SQLSERVER` by default and link system ODBC;
- non-Windows/default-disabled builds keep stubs that report unavailable ODBC support;
- Microsoft ODBC Driver detection is implemented through the native doctor helper;
- placeholders lower to `?`;
- default non-live tests cover redaction, missing-driver diagnostics, invalid options,
  pool state, and stdlib API shape;
- live tests are gated by `SLOPPY_SQLSERVER_TEST_CONNECTION_STRING`.

Acceptance:

- missing Microsoft ODBC Driver diagnostic matches this spec;
- configured integration test can query SQL Server;
- provider code stays behind provider-specific ODBC boundaries.

### Phase E: Dynamic Provider ABI

Introduce native provider ABI only after static first-party providers prove the boundary.

### Phase F: Driver Doctor Checks

Tasks:

- inspect plan `dataProviders`;
- detect missing drivers/libraries;
- report install guidance;
- avoid connecting when checking driver presence is enough.

Acceptance:

- `sloppy doctor` can report SQLite/PostgreSQL/SQL Server availability once CLI exists;
- diagnostics are redacted and actionable.

EPIC-19 starts the CLI side with deterministic metadata-driven `doctorChecks` and output
redaction. MAIN1-11 keeps doctor evidence-aware: it can report provider metadata presence
from a validated plan and can include deterministic fixture checks, but it does not connect
to live providers or enumerate machine-local SQL Server drivers by default. Future provider
doctor checks should reuse the native PostgreSQL and SQL Server helper APIs behind explicit
opt-in flags or environment gates.

### Phase G: Data Provider Plan Integration

Tasks:

- ensure provider modules emit plan entries;
- validate token/service/capability consistency;
- expose provider metadata to audit/routes/doctor.

Acceptance:

- provider plan fixtures are golden-tested;
- missing permission/config produces startup diagnostic.

## Acceptance Criteria

Provider foundation is accepted when:

- common API shape is documented;
- plan contribution schema exists;
- query template lowering is specified;
- resource lifecycle is defined;
- first SQLite story has tests and acceptance criteria;
- no database dependency exists before provider phase.

## Open Questions

- Exact row/result JS shape.
- Whether common API exposes typed schema mapping.
- Pool implementation ownership.
- Whether ODBC is Windows-only or later cross-platform for SQL Server.
