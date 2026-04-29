# Modules

Status: Bootstrap module skeleton implemented.

Purpose: document the current `Sloppy.module(...)` API, builder registration, dependency
ordering, phase execution, debug metadata, diagnostics, and future path to real plan
emission.

Implemented bootstrap API example:

```ts
const DataModule = Sloppy.module("data")
  .capabilities(caps => {
    caps.addDatabase("data.main", {
      provider: "sqlite",
      path: ":memory:",
      access: "readwrite",
    });
  })
  .services(services => {
    // Current bootstrap shape only: this throws bridge-unavailable until the
    // JavaScript-to-native SQLite resource bridge lands.
    services.addSingleton("data.main", () => data.sqlite.open({
      path: ":memory:",
    }));
  });

const UsersModule = Sloppy.module("users")
  .dependsOn("data")
  .metadata("area", "users")
  .services(services => {
    services.addSingleton("users.message", () => "hello");
  })
  .routes(app => {
    app.mapGroup("/users")
      .withTags("Users")
      .mapGet("/{id:int}", ({ route, services }) => Results.ok({
        id: route.id ?? "demo",
        message: services.get("users.message"),
      }))
      .withName("Users.Get");
  });

const builder = Sloppy.createBuilder();

builder
  .addModule(UsersModule)
  .addModule(DataModule);

const app = builder.build();
```

PostgreSQL uses the same module phase shape with provider-specific metadata:

```ts
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

SQL Server uses the same module phase shape with ODBC-specific metadata:

```ts
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

Implemented behavior:

- `Sloppy.module(name)` creates a declarative module object.
- Module names must be lowercase identifiers that start with a lowercase letter and then
  contain only lowercase letters, digits, dots, or hyphens.
- `.dependsOn(...names)` declares required module names.
- `.capabilities(fn)` registers a capabilities phase callback.
- `.services(fn)` registers a services phase callback.
- `.routes(fn)` registers a routes phase callback.
- `.metadata(key, value)` stores simple custom debug metadata.
- `builder.addModule(module)` accepts only modules created by `Sloppy.module(...)`.
- Module support is builder-only in this skeleton; `Sloppy.create()` builds immediately and
  does not accept modules.
- Adding a module freezes that module definition against further mutation.
- Duplicate module names fail when added to one builder.
- `builder.build()` resolves the module dependency graph before running module phases.
- Missing dependencies and dependency cycles fail with useful JavaScript errors.
- Capability phases run for all modules in dependency order before services, and services
  run before routes.
- Independent modules keep builder insertion order.
- Module-created capabilities, services, and routes are attributed with the contributing
  module name.
- `app.__debug().modules`, `app.__getModuleGraph()`, and
  `app.__getPlanContributions().modules` expose bootstrap-only module debug metadata.

Debug module entries currently include:

```js
{
  name: "data",
  dependencies: [],
  order: 0,
  contributes: ["capabilities", "services"],
  capabilities: ["data.main"],
  services: ["data.users"],
  routes: [],
  metadata: {}
}
```

Diagnostics are JavaScript `Error`/`TypeError` values for now. They include module names,
dependency names, phase names, and a short fix hint where practical.

Not implemented yet: compiler extraction, automatic `app.plan.json` emission, native
runtime module loading, module package distribution, native plugins, optional
dependencies, version ranges, JavaScript-to-native SQLite/PostgreSQL/SQL Server calls,
middleware, route filters, hot reload, and dynamic module loading after build.

CLI status: `sloppy audit --plan <path>` can inspect interim `modules` metadata for missing
dependencies and direct dependency cycles. This uses metadata fixtures/artifacts only; it
does not execute module phase callbacks or extract modules from source code.

Related internal docs: `docs/modularity.md`, `docs/app-plan.md`.
