# Plan-Backed Native Compiled Dispatch

Native endpoint dispatch is an internal optimization for Plan-backed web apps.
It does not change `app.get`, `app.post`, groups, modules, metadata chains, or
handler code.

## Current Contract

The compiler emits `routeDispatch` metadata and a `routes.slrt` binary route
artifact for web Plans. At runtime, Sloppy validates the artifact hash,
checksum, section bounds, route strings, handler IDs, methods, and execution
kinds before building the arena-owned native dispatch table from
`app.plan.json` route metadata:

- exact static paths use a method + path hash table;
- parameter routes use a method-specific native segment trie;
- the older first-static-segment candidate buckets remain as an internal
  fallback shape for manually constructed or partial dispatch tables;
- route constraints are enforced by the native route pattern matcher;
- `HEAD` dispatches through `GET` and response-body suppression stays at the
  transport boundary;
- `405 Method Not Allowed` can still build an `Allow` header from route
  metadata;
- named routes can generate native paths with percent-encoded parameter values
  and constraint roundtrip validation;
- `Results.text("literal")`, `Results.json(<literal>)`, and
  `Results.ok(<literal>)`, `Results.noContent()`, `Results.status(code)`
  without a body, and supported literal `Results.problem(...)` handlers can
  execute as native static responses without entering V8;
- schema-backed JSON request bodies can be validated natively before the
  handler boundary when the route has a `jsonRequest` native schema plan;
- static JSON response bodies use `jsonResponse` native static writer metadata;
- supported schema-backed JSON response bodies use `jsonResponse` native schema
  writer metadata;
- dynamic route metadata can fall back to the current generated JavaScript
  runtime path when V8 is enabled.

This is not a second public router. The source API remains the existing Sloppy
app/group/route registration API.

`SLOPPY_ROUTE_DISPATCH` can force the lookup lane for diagnostics. The value is
read when the route table is built and stored on the dispatch table:

- `compiled` or unset uses the native exact hash plus segment trie;
- `classic` uses the linear route-table matcher;
- `validate` runs both paths and fails the request if the selected binding or
  method-mismatch result differs.

`SLOPPY_JSON_DISPATCH` is a local diagnostics and benchmark switch:

- `native` or unset uses native schema-backed request validation and
  materialize-once body handoff when the Plan advertises it;
- `generic` forces the generic JSON body policy path for baseline comparison;
- `validate` keeps the native request path enabled and is reserved for
  differential checks where the runtime can compare supported generic/native
  behavior.

## Plan Metadata

`routeDispatch.mode` is `native-compiled` for compiler-emitted web artifacts.
That means Plan-backed native dispatch with `routes.slrt` integrity validation.
The Plan records the `routes.slrt` path and SHA-256 hash, route
counts, endpoint counts, exact static paths, parameter route counts, native
no-JS route counts, URL writer counts, candidate bucket counts, segment-trie
node counts, known constraints, aggregate JSON request/response
native/generic/fallback counts. Fallback reasons are exposed per route through
`routes[].jsonRequest` and `routes[].jsonResponse`.

`routes[].dispatch` records the endpoint ID, dispatch strategy, and execution
kind for each static Plan route. Execution kind is `v8-handler`,
`native-static-text`, `native-static-json`, `native-static-empty`, or
`native-static-problem`.

Native static response plans are eligible only when the compiler can prove the
response body, status, and content type from the route body. Static JSON,
static text, empty status responses such as `204`, and static problem responses
can bypass handler lookup and V8 execution. Dynamic handlers, plain object
returns, request-dependent values, and values that require runtime JavaScript
execution stay on the registered V8 handler path.

`routes[].jsonRequest` and `routes[].jsonResponse` record per-route JSON modes.
Fallback reasons are part of the contract: if the route cannot use native JSON,
inspection tools show why instead of silently implying success.

## Route Shapes

Native dispatch covers the route syntax currently accepted by
`sl_route_pattern_parse`:

- static segments;
- `{name}` and `{name:str}`;
- `{name:int}`;
- `{name:uuid}`;
- `{name:alpha}`;
- `{name:float}`;
- strict trailing-slash behavior;
- query strings excluded from route matching.

The current route syntax still does not include catch-all or regex route
segments:

- catch-all route dispatch remains zero because current route syntax does not
  support catch-all segments;
- regex constraints are not part of the current supported route syntax.

## Inspection

Use:

```sh
sloppy routes .sloppy --dispatch
sloppy doctor .sloppy --dispatch
```

`routes` shows the Plan-backed dispatch table shape, artifact path/hash, route execution
kind, constraints, fallback counts, and URL generation status. `doctor` reports
route dispatch mode and artifact hash so package and outside-checkout runs can
verify they are using the same route artifact as the Plan.
