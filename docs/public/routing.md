# Routing

Status: Bootstrap `mapGet` and route group shape implemented with structural app freeze.

Bootstrap status: `Sloppy.create()` and `Sloppy.createBuilder().build()` return an
in-memory app facade with `app.mapGet(...)` and `app.mapGroup(...)`. Handler registration,
HTTP dispatch integration, app-plan emission, and compiler extraction remain future work.

Purpose: document current `app.mapGet`, route snapshots, handler context, and future route
features.

Implemented bootstrap API example:

```ts
app.mapGet("/hello", ({ services }) => Results.text(services.get("message")))
  .withName("Hello");

const users = app
  .mapGroup("/users")
  .withTags("Users");

users.mapGet("{id:int}", ({ route }) => Results.ok({ id: route.id ?? "demo" }))
  .withName("Users.Get");
```

`app.mapGet(pattern, handler)` and `app.mapGet(pattern, metadata, handler)` currently:

- requires `pattern` to be a non-empty string starting with `/`;
- requires `handler` to be a function;
- store a `{ method: "GET", pattern, handler, name: null, metadata }` registration in
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
  log: app.log,
  route: {}
}
```

This context exists only for bootstrap tests and examples. `route` is currently an empty
object unless a test or caller supplies an explicit context. Native HTTP dispatch still does
not materialize route params into JavaScript handler context.

Route groups:

- `app.mapGroup(prefix)` requires a non-empty prefix starting with `/`.
- The returned group exposes `.prefix`, `.withTags(...tags)`, `.withName(name)`, and
  `.mapGet(pattern, handler)` / `.mapGet(pattern, metadata, handler)`.
- Prefixes normalize trailing slashes except for root `/`.
- Child patterns may start with `/` or be relative.
- `"/users"` plus `"{id:int}"` and `"/users/"` plus `"/active"` normalize to
  `"/users/{id:int}"` and `"/users/active"`.
- A child pattern of `/` maps to the group prefix.
- Group tags, group name, and group prefix are copied into child route metadata.
- Route groups are in-memory bootstrap metadata only.

Not implemented yet: `mapPost`, nested groups, middleware, filters, automatic validation,
duplicate/ambiguity diagnostics, compiler extraction, automatic `app.plan.json` emission,
native route table construction, HTTP server behavior, and route dispatch integration.

Related internal docs: `docs/developer-ergonomics.md`, `docs/app-plan.md`,
`docs/execution-model.md`.
