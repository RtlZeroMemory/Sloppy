# Services

Status: Bootstrap string-token services skeleton implemented.

Purpose: document the current minimal service registry/provider API and the future path to
request-scoped lifetimes, disposal, modules, and plan-visible service graphs.

Implemented API example:

```ts
const builder = Sloppy.createBuilder();

builder.services.addSingleton("message", () => "Hello");
builder.services.addTransient("clock", () => ({ now: () => 123 }));
builder.addModule(Sloppy.module("users")
  .services(services => {
    services.addSingleton("users.message", () => "Hello from users");
  }));

const app = builder.build();
const services = app.services.createScope();

services.get("message");
services.get("clock");
```

Implemented behavior:

- Service tokens must be non-empty strings.
- `addSingleton(token, factoryOrValue)` registers a singleton.
- Singleton factories are called lazily on first resolution and then cached.
- Singleton non-function values are returned as supplied.
- `addTransient(token, factory)` registers a transient factory.
- Transient factories are called on every `get`.
- Duplicate service tokens fail during registration.
- Missing service tokens fail during resolution with a helpful error.
- Module service phases can register singleton and transient services through the same
  service builder.
- Services registered during a module services phase are attributed in
  `app.__debug().modules[].services`.
- `builder.build()` freezes further service registration.
- `app.services.get(token)` resolves through a short-lived scope.
- `app.services.createScope()` returns a scope with `scope.get(token)` and
  `scope.capabilities`.

The current scope object is a tiny resolution context only. It is not a real request
lifetime and it does not own disposal.

`examples/ergonomics/app.js` uses a singleton service from a grouped route handler to show
the current bootstrap handler context shape. This is still JavaScript-only route snapshot
behavior, not native request-scope DI.

`examples/data-foundation/app.js` shows a fake data provider registered as the
`data.main` service alongside matching database capability metadata. This service is still
an ordinary bootstrap singleton; it does not open a database connection.

`examples/sqlite-basic/app.js` shows the intended SQLite service registration through
`data.sqlite.open({ path: ":memory:" })`. In a V8-enabled runtime that installs the SQLite
bridge, that factory returns a safe wrapper around a resource-table connection handle.
This source-stdlib example is still not a `sloppy run` executable tutorial; native SQLite
execution is covered by C tests and the bridge is covered by V8-gated fixtures.

`examples/postgres-basic/app.js` shows the intended PostgreSQL service registration through
`data.postgres.open({ connectionString, maxConnections })`. That factory currently fails
with an honest bridge-unavailable error if resolved from JavaScript. Native PostgreSQL
execution is covered by C tests, with live database coverage gated by
`SLOPPY_POSTGRES_TEST_URL`.

`examples/sqlserver-basic/app.js` shows the intended SQL Server service registration
through `data.sqlserver.open({ connectionString, maxConnections })`. That factory
currently fails with an honest bridge-unavailable error if resolved from JavaScript. Native
SQL Server execution is covered by C tests, with live ODBC coverage gated by
`SLOPPY_SQLSERVER_TEST_CONNECTION_STRING`.

Not implemented yet: request-scoped lifetimes, disposal hooks, async factories, service
dependency graph validation, service cycle diagnostics, typed tokens, decorators, native
service graph validation, and real plan emission.

Related internal docs: `docs/developer-ergonomics.md`, `docs/modularity.md`,
`docs/diagnostics.md`.
