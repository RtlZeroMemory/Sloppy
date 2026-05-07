# Ergonomics Example

Status: Bootstrap developer ergonomics API-shape example.

This example shows the current bootstrap surface: route groups, result helpers,
schema metadata, config, logging, and services. It uses the source stdlib path because the
future bare `"sloppy"` import is not wired into compiler/runtime module resolution yet.

What works today:

- `Sloppy.createBuilder()` creates the JavaScript-only bootstrap builder.
- Config, memory logging, and string-token services can be registered before `build()`.
- `app.mapGroup("/users")` creates an in-memory route group.
- Group child routes compose prefixes, so `"/users"` plus `"{id:int}"` becomes
  `"/users/{id:int}"`.
- `withTags("Users")` and `withName("Users")` store group metadata on child route
  snapshots.
- `Results.ok`, `Results.accepted`, and `Results.noContent` create frozen descriptors.
- `schema.object(...)` and `schema.string().min(...)` create inspectable validation
  metadata and standalone validators.
- CTest statically verifies this example imports the current stdlib path and uses the
  expected API shape.

What does not work yet:

- this source-stdlib example is not a `sloppy run --artifacts` app.
- `sloppyc` does not compile this example or extract route groups/schemas from it.
- This example does not emit `app.plan.json`.
- The current bounded `sloppy run` path does not load this source-stdlib example, materialize a
  request context, or parse request bodies/query values for it.
- Route params are not passed by the native HTTP runtime into JavaScript handler context
  yet; this example uses `route.id ?? "demo"` to stay honest in bootstrap mode.
- Validation metadata is not wired to automatic `400` responses.
- There is no OpenAPI generation.
- There is no `app.run` or `app.listen` yet.

Until compiler extraction, app-plan emission, ESM bootstrap loading, and HTTP serving land,
this directory is a checked-in documentation and fixture example rather than a runnable
application host.
