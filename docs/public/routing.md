# Routing

Status: Bootstrap `mapGet`, route group shape, compiler extraction MVP, and dev-only GET
artifact routing implemented.

Bootstrap status: `Sloppy.create()` and `Sloppy.createBuilder().build()` return an
in-memory app facade with `app.mapGet(...)` and `app.mapGroup(...)`. Compiler-emitted
`mapGet` metadata can now be consumed by `sloppy run --artifacts` for GET-only dev
dispatch. Bootstrap route snapshots, module-contributed routes, request contexts, and
route params in JavaScript remain future work.

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
- records `metadata.module` when the route was registered by a module routes phase;
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
object unless a test or caller supplies an explicit context. Native dev dispatch can match
route parameters to select a handler, but it still does not materialize route params into
the JavaScript handler context.

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
- Module-created grouped routes use the same shape and also include `metadata.module`.
- Route groups are in-memory bootstrap metadata only.

Compiler extraction MVP:

- supports `app.mapGet("/literal", () => Results.text(...))`;
- supports `app.mapGet("/literal", () => Results.json(...))`;
- supports `const group = app.mapGroup("/prefix"); group.mapGet("/child", handler)`;
- supports `.withName("Route.Name")`;
- requires compiled handlers to declare zero parameters because the current runtime calls
  handler exports with zero arguments;
- requires handler result arguments in compiled routes to be inline JSON-safe literals,
  arrays, or object literals;
- assigns handler IDs from `1` in source order;
- emits route metadata into `app.plan.json` as `method`, `pattern`, `handlerId`, and
  `name`;
- rejects dynamic route strings, computed method names, unsupported handler bodies,
  TypeScript input, closed-over source-file bindings, conditional route registration,
  loops, modules, middleware, and package resolution.

Dev-only run behavior:

- reads the compiler-emitted `routes` metadata section from `app.plan.json`;
- supports GET route bindings only;
- parses each route pattern with the existing native route parser;
- matches incoming request paths with strict trailing-slash behavior;
- resolves the matched route to a numeric handler ID and validates that ID against the
  parsed Plan handler table before entering V8;
- returns `404` when no GET route matches and `405` for unsupported methods.

Not implemented yet: `mapPost`, nested groups, middleware, filters, automatic validation,
production route table construction, route precedence optimization, route params in handler
context, and route/module extraction beyond the tiny compiler MVP shape above.

## CLI Introspection

`sloppy routes --plan <path>` can print route metadata from a plan-compatible metadata
fixture or artifact, including the narrow route metadata emitted by the compiler MVP. This
is inspection-only: it does not execute handlers, start HTTP, enter V8, or build a
production native route table. `sloppy run --artifacts <dir>` also reads the documented
interim `routes` metadata section for dev-only GET dispatch until native Plan parsing grows
a real route section.

`sloppy openapi --plan <path>` uses the same route metadata to emit a minimal OpenAPI
skeleton. It converts Sloppy route parameters such as `{id}` and `{id:int}` into OpenAPI
path parameters, but it does not generate schemas or request/response bodies.

Related internal docs: `docs/developer-ergonomics.md`, `docs/app-plan.md`,
`docs/execution-model.md`.
