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

- sqlite;
- libpq;
- ODBC;
- connection pools;
- SQL parsing;
- provider native ABI;
- database JS API.

## Current Phase

`src/data/` is a placeholder. No data provider dependencies are present.
The EPIC-14 module skeleton can register fake JavaScript services from modules, but it does
not implement data providers, database modules, provider resources, or provider plan
entries.

## Future Phase Order

1. SQLite first, built-in/static provider.
2. PostgreSQL second, via libpq.
3. SQL Server third, via Microsoft ODBC Driver and ODBC API on Windows.
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

Do not add provider directories with real dependencies before their phase.

## Internal Architecture

The JS API sends query templates to a Sloppy data service. The runtime validates resource
IDs, permissions, and provider configuration. Provider implementations own SQL lowering,
binding, execution, and result conversion behind a common interface.

## Public API Shape

SQLite:

```ts
import { sqlite } from "sloppy:data/sqlite";

builder.addModule(
  sqlite.module({
    token: "data.main",
    path: "app.db",
  })
);
```

PostgreSQL:

```ts
import { postgres } from "sloppy:data/postgres";

builder.addModule(
  postgres.module({
    token: "data.main",
    connectionString: builder.config.require("DATABASE_URL"),
    pool: {
      min: 1,
      max: 20,
    },
  })
);
```

SQL Server:

```ts
import { sqlserver } from "sloppy:data/sqlserver";

builder.addModule(
  sqlserver.module({
    token: "data.main",
    connectionString: builder.config.require("SQLSERVER_CONNECTION"),
  })
);
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

All resources use the Sloppy resource table with generation counters. JS never receives raw
C pointers.

Resource rules:

- pool resources are app-lifetime singleton-style resources;
- connection resources are checked out from a pool and returned deterministically;
- statement resources are closed when execution completes unless explicitly prepared;
- transaction resources pin a connection until commit/rollback;
- debug builds report leaked statements, connections, and transactions at request or app
  shutdown.

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

Missing config:

```text
sloppy: database configuration missing

  Provider:
    postgres

  Token:
    data.main

  Missing key:
    DATABASE_URL
```

Parameter binding diagnostic:

```text
sloppy: unsupported database parameter

  Provider:
    postgres

  Token:
    data.main

  Parameter:
    2

  Reason:
    value type cannot be bound by this provider
```

## Distribution Implications

SQLite is easiest: static or bundled. PostgreSQL requires libpq DLL strategy. SQL Server
depends on Microsoft ODBC Driver presence on Windows. `sloppy doctor` should detect missing
drivers and incomplete config.

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

Tasks:

- add ODBC integration intentionally;
- use Microsoft ODBC Driver on Windows;
- implement driver detection;
- lower placeholders to `?`;
- add env-gated integration tests;
- add `sloppy doctor` provider checks later.

Acceptance:

- missing Microsoft ODBC Driver diagnostic matches this spec;
- configured integration test can query SQL Server;
- provider code stays behind platform/provider boundaries.

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
