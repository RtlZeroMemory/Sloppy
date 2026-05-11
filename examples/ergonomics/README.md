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

Current product state:

- This source-stdlib example is documentation-first and checked by CTest as an API-shape
  fixture.
- `sloppy run --artifacts` currently runs emitted artifacts such as
  `examples/compiler-hello`.
- `sloppyc` compilation, route-group/schema extraction, and `app.plan.json`
  emission for this broader builder shape are planned separately.
- The bounded `sloppy run` path currently loads generated artifacts. Source-shape loading,
  request-context materialization, and request body/query parsing for this example belong
  to later app-host runtime work.
- Native HTTP route parameters reach JavaScript handler context in later runtime work; this
  example uses `route.id ?? "demo"` to stay honest in bootstrap mode.
- Validation metadata currently remains route metadata. Automatic `400` responses belong
  to a later validation pipeline.
- OpenAPI generation is planned separately.
- `app.run` and `app.listen` belong to later app-host runtime work.

This directory remains a checked-in documentation and fixture example for the current
application host shape.
