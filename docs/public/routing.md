# Routing

Status: Bootstrap route declaration, route group shape, ENGINE-02 compiler route metadata,
dev-only artifact routing, and minimal route/query/header/body request context implemented.

Bootstrap status: `Sloppy.create()` and `Sloppy.createBuilder().build()` return an
in-memory app facade with `app.mapGet(...)`, `app.mapPost(...)`, `app.mapPut(...)`,
`app.mapPatch(...)`, `app.mapDelete(...)`, and `app.mapGroup(...)`. Compiler-emitted
GET/POST/PUT/PATCH/DELETE metadata is validated, inspected, and consumed by the dev-only
HTTP runtime. Matched route params, query params, request headers, and bounded JSON/text
bodies are passed to V8 handlers in a minimal request context. Bootstrap route snapshots and
module-contributed routes remain JavaScript-only until later compiler/runtime work.

Purpose: document current route declaration, route snapshots, handler context, and future
route features.

ENGINE-01 target contract:

- core route declaration methods are `mapGet`, `mapPost`, `mapPut`, `mapPatch`, and
  `mapDelete`;
- OPTIONS is framework-owned for allowed-method/preflight-style responses;
- HEAD is deferred until its body and metadata policy is explicit;
- route params are available as strings on `ctx.route`;
- query params are decoded scalar strings with last-wins repeated-key behavior;
- request headers are available through case-insensitive `ctx.request.headers.get(name)`
  and deterministic entries;
- JSON and text bodies are available in the dev runtime with body-size and content-type
  policy;
- multipart/file upload, streaming bodies, nested groups, middleware, filters, typed
  binding, and production route-table optimization remain deferred.

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

`app.mapGet`, `app.mapPost`, `app.mapPut`, `app.mapPatch`, and `app.mapDelete` currently:

- requires `pattern` to be a non-empty string starting with `/`;
- requires `handler` to be a function;
- store a `{ method, pattern, handler, name: null, metadata }` registration in memory;
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

Native dev dispatch passes this implemented ENGINE-04 runtime context to compiled handlers:

```js
{
  route: { id: "123" },
  query: { q: "abc" },
  request: {
    method: "POST",
    path: "/users/123",
    rawTarget: "/users/123?q=abc",
    headers: {
      get(name) {},
      entries() {}
    },
    text() {},
    json() {}
  }
}
```

Route params and query values are strings. `{id:int}` validates the path segment but does
not coerce `route.id` to a number. Query parsing splits on `&`, splits key/value on the
first `=`, allows empty values, uses last-wins for repeated keys, decodes `%XX`, and treats
`+` as a space. Invalid percent escapes fail safely. The dev runtime bounds request targets
with `SL_HTTP_DEFAULT_MAX_TARGET_LENGTH` and query pairs with
`SL_HTTP_DEFAULT_MAX_QUERY_PARAMS`. Request bodies are bounded by
`SL_HTTP_DEFAULT_MAX_BODY_LENGTH`. Bodies without `Content-Type`, unsupported content
types, malformed JSON, transfer-encoded bodies, and oversized bodies fail before handler
execution. Supported body media types are `application/json`, `application/*+json`, and
`text/plain`. `ctx.request.headers.get(name)` is case-insensitive and duplicate header
values are comma-joined deterministically.

MAIN1-13 conformance compiles `examples/request-context/app.js` and, when V8 is enabled,
runs `GET /users/123?q=abc&q=last` through `sloppy run --artifacts --once` to verify
route params, last-wins query behavior, method, path, and raw target through the real
artifact boundary.

Route groups:

- `app.mapGroup(prefix)` requires a non-empty prefix starting with `/`.
- The returned group exposes `.prefix`, `.withTags(...tags)`, `.withName(name)`, and the
  same `mapGet`/`mapPost`/`mapPut`/`mapPatch`/`mapDelete` route methods.
- Prefixes normalize trailing slashes except for root `/`.
- Child patterns may start with `/` or be relative.
- `"/users"` plus `"{id:int}"` and `"/users/"` plus `"/active"` normalize to
  `"/users/{id:int}"` and `"/users/active"`.
- A child pattern of `/` maps to the group prefix.
- Group tags, group name, and group prefix are copied into child route metadata.
- Module-created grouped routes use the same shape and also include `metadata.module`.
- Route groups are in-memory bootstrap metadata only.

Compiler extraction:

- supports `app.mapGet("/literal", () => Results.text(...))`;
- supports `app.mapGet("/literal", () => Results.json(...))`;
- supports `app.mapPost`, `app.mapPut`, `app.mapPatch`, and `app.mapDelete` as plan
  metadata;
- supports `const group = app.mapGroup("/prefix"); group.mapGet("/child", handler)`;
- supports `.withName("Route.Name")`;
- supports direct async handlers as metadata/emitted JavaScript and V8-gated run-once
  execution when the returned Promise settles during the owner-thread microtask drain;
- allows compiled handlers to declare zero parameters or one simple identifier request
  context parameter;
- supports inline JSON-safe literals, arrays, object literals, and simple context property
  reads such as `route.id` and `query.q` in result arguments;
- assigns handler IDs from `1` in source order;
- emits route metadata into `app.plan.json` as `method`, `pattern`, `handlerId`, `name`,
  and source metadata, and the native Plan parser now validates that route metadata;
- rejects dynamic route strings, computed method names, unsupported handler bodies,
  TypeScript input, closed-over source-file bindings, conditional route registration,
  loops, HEAD/OPTIONS route declarations, modules, middleware, and package resolution.

`docs/compiler-supported-syntax.md` is the source of truth for the supported
syntax matrix and fixture expectations.

Dev-only run behavior:

- reads the compiler-emitted `routes` metadata section from `app.plan.json`;
- supports GET/POST/PUT/PATCH/DELETE route bindings;
- parses each route pattern with the existing native route parser;
- rejects malformed route sections, duplicate method/pattern pairs, duplicate non-empty
  route names, and missing handler references during plan startup validation;
- builds a native route table before serving;
- orders literal route patterns before parameter route patterns and preserves source order
  when precedence is equal;
- matches incoming request paths with strict trailing-slash behavior;
- resolves the matched route to a numeric handler ID and validates that ID against the
  parsed Plan handler table before entering V8;
- returns `404` when no route matches, `405` for method mismatches, `400` for malformed
  JSON bodies, `413` for oversized bodies, `415` for unsupported content types, and `501`
  for unsupported transfer/body framing;
- returns safe dev `500` responses for handler exceptions or malformed/unsupported result
  descriptors.

Not implemented yet: nested groups, middleware, filters, automatic validation, production
route table construction, route precedence optimization, multipart/upload or streaming
bodies, and route/module extraction beyond the compiler shape above.

## CLI Introspection

`sloppy routes --plan <path>` can print route metadata from a plan-compatible metadata
fixture or artifact, including the route metadata emitted by the compiler. This
is inspection-only: it does not execute handlers, start HTTP, enter V8, or build a
production native route table. `sloppy run --artifacts <dir>` also reads the documented
Plan v1 alpha `routes` section for dev-only local dispatch.

`sloppy openapi --plan <path>` uses the same route metadata to emit a minimal OpenAPI
skeleton. It converts Sloppy route parameters such as `{id}` and `{id:int}` into OpenAPI
path parameters, but it does not generate schemas or request/response bodies.

Related internal docs: `docs/developer-ergonomics.md`, `docs/app-plan.md`,
`docs/execution-model.md`.
