# Ergonomics Example

Bootstrap developer ergonomics API-shape example.
This example shows the current bootstrap surface: route groups, result helpers,
schema metadata, config, logging, and services. It uses the current bootstrap stdlib
source path and is validated as an API-shape example.

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

Current limitations:

- This source-stdlib example is documentation-first and is not on the `sloppy run --artifacts` lane.
- `sloppyc` compilation and route-group/schema extraction are still pending for this shape.
- `app.plan.json` is not emitted for this example yet.
- The current bounded `sloppy run` path does not load this source shape, materialize a request
  context, or parse request bodies/query values for it.
- Route params are not passed by the native HTTP runtime into JavaScript handler context
  yet; this example uses `route.id ?? "demo"` to stay honest in bootstrap mode.
- Validation metadata is not wired to automatic `400` responses yet.
- OpenAPI generation is planned separately.
- `app.run` and `app.listen` are not available yet.

Until compiler extraction, app-plan emission, ESM bootstrap loading, and HTTP serving land,
this directory remains a checked-in documentation and fixture example rather than a runnable
application host.
