# Routing

Status: Tiny `mapGet` bootstrap shape implemented.

Bootstrap status: `Sloppy.create()` returns an in-memory app facade with `app.mapGet(...)`.
Route groups, handler registration, HTTP dispatch integration, and compiler extraction
remain future work.

Purpose: document future `app.mapGet`, `app.mapPost`, route groups, route parameters,
metadata, and route diagnostics.

Implemented bootstrap API example:

```ts
app.mapGet("/hello", () => Results.text("hello")).withName("Hello");
```

`app.mapGet(pattern, handler)` currently:

- requires `pattern` to be a non-empty string starting with `/`;
- requires `handler` to be a function;
- stores a `{ method: "GET", pattern, handler, name: null, metadata: {} }` registration in
  memory;
- returns a frozen endpoint builder with `.withName(name)`;
- lets tests/debug code inspect frozen route snapshots with `app.__getRoutes()`.

`withName(name)` requires a non-empty string, stores it as the route `name`, and returns the
same endpoint builder for future chaining shape.

Not implemented yet: route groups, `mapPost`, middleware, filters, validation, route
metadata beyond `name`, duplicate/ambiguity diagnostics, compiler extraction, automatic
`app.plan.json` emission, native route table construction, HTTP server behavior, and route
dispatch integration.

Related internal docs: `docs/developer-ergonomics.md`, `docs/app-plan.md`,
`docs/execution-model.md`.
