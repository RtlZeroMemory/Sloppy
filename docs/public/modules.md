# Modules

Status: Bootstrap module skeleton implemented.

Purpose: document the current `Sloppy.module(...)` API, builder registration, dependency
ordering, phase execution, debug metadata, diagnostics, and future path to real plan
emission.

Implemented bootstrap API example:

```ts
const DataModule = Sloppy.module("data")
  .services(services => {
    services.addSingleton("data.users", () => new Map());
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

Implemented behavior:

- `Sloppy.module(name)` creates a declarative module object.
- Module names must be lowercase identifiers that start with a lowercase letter and then
  contain only lowercase letters, digits, dots, or hyphens.
- `.dependsOn(...names)` declares required module names.
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
- Services run for all modules in dependency order before routes run for all modules.
- Independent modules keep builder insertion order.
- Module-created services and routes are attributed with the contributing module name.
- `app.__debug().modules`, `app.__getModuleGraph()`, and
  `app.__getPlanContributions().modules` expose bootstrap-only module debug metadata.

Debug module entries currently include:

```js
{
  name: "users",
  dependencies: ["data"],
  order: 1,
  contributes: ["services", "routes", "metadata"],
  services: ["users.message"],
  routes: ["GET /users/{id:int}"],
  metadata: { area: "users" }
}
```

Diagnostics are JavaScript `Error`/`TypeError` values for now. They include module names,
dependency names, phase names, and a short fix hint where practical.

Not implemented yet: compiler extraction, automatic `app.plan.json` emission, native
runtime module loading, module package distribution, native plugins, optional
dependencies, version ranges, data providers, middleware, route filters, hot reload, and
dynamic module loading after build.

Related internal docs: `docs/modularity.md`, `docs/app-plan.md`.
