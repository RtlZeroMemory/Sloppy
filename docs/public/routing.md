# Routing

Status: Tiny `mapGet` bootstrap shape implemented with structural app freeze.

Bootstrap status: `Sloppy.create()` and `Sloppy.createBuilder().build()` return an
in-memory app facade with `app.mapGet(...)`. Route groups, handler registration, HTTP
dispatch integration, and compiler extraction remain future work.

Purpose: document current `app.mapGet`, route snapshots, handler context, and future route
features.

Implemented bootstrap API example:

```ts
app.mapGet("/hello", ({ services }) => Results.text(services.get("message")))
  .withName("Hello");
```

`app.mapGet(pattern, handler)` currently:

- requires `pattern` to be a non-empty string starting with `/`;
- requires `handler` to be a function;
- stores a `{ method: "GET", pattern, handler, name: null, metadata: {} }` registration in
  memory;
- returns a frozen endpoint builder with `.withName(name)`;
- fails after `app.freeze()`;
- lets tests/debug code inspect frozen route snapshots with `app.__getRoutes()`.

`withName(name)` requires a non-empty string, stores it as the route `name`, and returns the
same endpoint builder for future chaining shape. It also fails after `app.freeze()`.

Route handlers exposed through `app.__getRoutes()` receive a minimal context when invoked
without an explicit context:

```js
{
  services: app.services.createScope(),
  config: app.config,
  log: app.log
}
```

This context exists only for bootstrap tests and examples. It is not connected to native
HTTP dispatch, request parsing, route params, or real request scopes.

Not implemented yet: route groups, `mapPost`, middleware, filters, validation, route
metadata beyond `name`, duplicate/ambiguity diagnostics, compiler extraction, automatic
`app.plan.json` emission, native route table construction, HTTP server behavior, and route
dispatch integration.

Related internal docs: `docs/developer-ergonomics.md`, `docs/app-plan.md`,
`docs/execution-model.md`.
