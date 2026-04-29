# App Model

Status: Bootstrap app-host skeleton, compiler extraction MVP, and dev-only artifact run MVP
implemented.

Bootstrap status: `stdlib/sloppy/app.js` exports a frozen `Sloppy` object with
`Sloppy.create()`, `Sloppy.createBuilder()`, and `Sloppy.module(...)`. The returned app is
an in-memory conceptual object with route registration, route groups, structural freeze
behavior, config/log/services accessors, module debug metadata, and `app.__getRoutes()` for
bootstrap tests/debugging.

Compiler status: `sloppyc build` can extract one tiny app from either `Sloppy.create()` or
`Sloppy.createBuilder()` plus `builder.build()`, then emit deterministic `app.plan.json`,
`app.js`, and placeholder `app.js.map` artifacts. The compiler MVP supports only literal
`mapGet` routes and simple route groups. It does not execute the bootstrap app object,
extract modules/services/data providers, or run `app.run`.
See `docs/compiler-supported-syntax.md` for the exact supported and rejected compiler
source matrix.

Runtime status: `sloppy run --artifacts <dir>` can load EPIC-21/24 artifacts in a
V8-enabled build, load the classic bootstrap runtime asset, evaluate the generated
classic-script `app.js`, validate registered handler IDs, route GET request paths using the
compiler-emitted route metadata, build a deterministic native route table, call handlers by
numeric ID with a minimal route/query/request context, and return supported
text/JSON/empty/problem responses. Unsupported request bodies fail before handler
execution.
Source input build handoff, native app-host validation, true bootstrap ESM loading,
services, middleware, and `app.run` remain deferred.

Purpose: explain the current builder/app model, structural freeze boundary, and the future
path to native app-host validation.

Implemented bootstrap builder example:

```ts
const builder = Sloppy.createBuilder();

builder.config.addObject({
  "app.name": "hello",
});

builder.logging.addMemorySink();
builder.services.addSingleton("message", () => "Hello from Sloppy");
builder.addModule(Sloppy.module("hello")
  .services(services => {
    services.addSingleton("hello.module", () => "Hello from a module");
  }));

const app = builder.build();

app.mapGet("/", ({ services }) => Results.text(services.get("message")));
app.freeze();
```

`Sloppy.create()` remains supported. It is currently equivalent to creating a default
builder and immediately calling `build()`; the returned app still allows routes to be
registered until `app.freeze()`.

Implemented lifecycle behavior:

- `Sloppy.createBuilder()` creates a builder with `config`, `logging`, and `services`.
- `Sloppy.module(name)` creates a bootstrap module definition with dependencies, services,
  routes, and simple metadata.
- `builder.addModule(module)` registers a module definition for build-time phase execution.
- `builder.build()` freezes further builder mutation and returns an app.
- Calling `builder.build()` again fails because building is a builder mutation.
- `builder.build()` resolves module dependencies and runs module services before module
  routes.
- The app starts mutable for route registration.
- `app.freeze()` is idempotent, returns the app, and freezes route/endpoint mutation.
- `app.isFrozen()` reports the route graph freeze state.
- `app.mapGet(...)`, `app.mapGroup(...)`, group metadata mutation, and endpoint
  `.withName(...)` fail after `app.freeze()`.
- `app.__debug().modules` exposes bootstrap-only module order and contribution metadata.
- Freeze is structural only. It does not run native startup validation, emit a plan, or
  start execution.

Current route registration shape:

```js
{
  method: "GET",
  pattern: "/hello",
  handler,
  name: null,
  metadata: {
    tags: [],
    groupName: null,
    groupPrefix: "/users"
  }
}
```

`app.__getRoutes()` returns frozen snapshots of the registered routes. It exists only for
bootstrap tests/debugging until compiler extraction and runtime host integration exist.

Route handlers invoked through the route snapshot receive a minimal context when no
explicit context is supplied:

```js
{
  services: app.services.createScope(),
  config: app.config,
  log: app.log,
  route: {}
}
```

Validation schemas can be attached as metadata, for example
`app.mapGet("/search", { query: schema.object({ q: schema.string().min(1) }) }, handler)`.
The bootstrap app stores this metadata only. It does not parse requests or produce automatic
validation responses.

Not implemented yet: `app.run`, `app.listen`, native startup validation for the full app
host, true bootstrap ESM loading, nested route groups, middleware, filters, automatic
validation, typed request binding beyond the EPIC-23 route/query/request context,
config file/env providers, console/file/native logging, service disposal, async factories,
real request-scoped lifetimes, module packages, data providers, native plugins, broad
bundling, Node/npm package resolution, arbitrary import graphs, or full TypeScript type
checking.

Related internal docs: `docs/developer-ergonomics.md`, `docs/modularity.md`,
`docs/app-plan.md`.
