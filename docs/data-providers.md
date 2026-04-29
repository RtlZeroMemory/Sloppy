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

- JavaScript-to-native SQLite resource/intrinsic integration;
- production connection pools;
- SQL parsing;
- provider plugin ABI;
- runnable database JS API.

## Current Phase

EPIC-15 added the JavaScript-only data/capabilities foundation: database capability
metadata, query template lowering, a fake data provider contract for tests/examples, and
transaction callback semantics.

EPIC-16 adds the first real provider boundary: native C SQLite support backed by the vcpkg
`sqlite3` package. The native provider can open `:memory:` databases, execute parameterized
statements, query rows, return the first row or no-row for `queryOne`, bind primitive
parameters, and commit or roll back explicit transactions.

The JavaScript stdlib now exposes `data.sqlite` metadata and `data.sqlite.open(options)` as
the intended public entry point, but that function fails honestly until the runtime has a
stdlib-to-native intrinsic/resource bridge. No native pointer is exposed to JavaScript and
no fake SQLite success is reported by the stdlib.

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

## Future Phase Order

1. SQLite first, built-in/static provider. Native C provider implemented; JavaScript bridge
   deferred.
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
      path: ":memory:",
      access: "readwrite",
    });
  })
  .services(services => {
    services.addSingleton("data.main", () => data.sqlite.open({
      path: ":memory:",
      access: "readwrite",
    }));
  })
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
- `data.sqlite.open(options)` validates SQLite options and then reports that the native
  stdlib bridge is unavailable.
- `data.postgres.open(options)` validates PostgreSQL connection string options, redacts
  credentials, and then reports that the native stdlib bridge is unavailable.
- `data.sqlserver.open(options)` validates SQL Server ODBC connection string options,
  redacts `PWD`, `Password`, and access tokens, and then reports that the native stdlib
  bridge is unavailable.
- `data.sqlserver.doctor(options)` returns a small bridge-unavailable doctor object in
  JavaScript; native C tests cover real ODBC driver-manager/driver detection.
- native SQLite provider tests use the same `?` placeholder lowering contract at the C
  boundary.
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
- parameters support null, text, integer, float, and boolean as 0/1;
- unsupported parameter kinds fail before unsafe coercion;
- nested transactions are rejected for now;
- statements are finalized on all paths and close is deterministic.

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
- SQLite MAIN1-08 bridge work depends on this table and should reuse it directly.

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
SQLite, libpq, ODBC, or filesystem work yet. Real provider work requires future real
worker threads or nonblocking provider integration plus thread-safe completion posting back
to the owning `SlLoop`.

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
redaction. It does not connect to live providers or enumerate machine-local SQL Server
drivers by default. Future provider doctor checks should reuse the native PostgreSQL and
SQL Server helper APIs behind explicit opt-in flags or environment gates.

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
