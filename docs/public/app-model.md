# App Model

Status: Bootstrap app-host skeleton implemented.

Bootstrap status: `stdlib/sloppy/app.js` exports a frozen `Sloppy` object with
`Sloppy.create()` and `Sloppy.createBuilder()`. The returned app is an in-memory conceptual
object with route registration, structural freeze behavior, config/log/services accessors,
and `app.__getRoutes()` for bootstrap tests/debugging.

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

const app = builder.build();

app.mapGet("/", ({ services }) => Results.text(services.get("message")));
app.freeze();
```

`Sloppy.create()` remains supported. It is currently equivalent to creating a default
builder and immediately calling `build()`; the returned app still allows routes to be
registered until `app.freeze()`.

Implemented lifecycle behavior:

- `Sloppy.createBuilder()` creates a builder with `config`, `logging`, and `services`.
- `builder.build()` freezes further builder mutation and returns an app.
- Calling `builder.build()` again fails because building is a builder mutation.
- The app starts mutable for route registration.
- `app.freeze()` is idempotent, returns the app, and freezes route/endpoint mutation.
- `app.isFrozen()` reports the route graph freeze state.
- `app.mapGet(...)` and endpoint `.withName(...)` fail after `app.freeze()`.
- Freeze is structural only. It does not run native startup validation, emit a plan, or
  start execution.

Current route registration shape:

```js
{
  method: "GET",
  pattern: "/hello",
  handler,
  name: null,
  metadata: {}
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
  log: app.log
}
```

Not implemented yet: `app.run`, `app.listen`, native startup validation, compiler
extraction, automatic `app.plan.json` emission, HTTP serving, modules, route groups,
middleware, filters, validation, config file/env providers, console/file/native logging,
service disposal, async factories, and real request-scoped lifetimes.

Related internal docs: `docs/developer-ergonomics.md`, `docs/modularity.md`,
`docs/app-plan.md`.
