# Modules Basic Example

Bootstrap module example.
This example shows module composition with `Sloppy.module(...)`, `dependsOn`, services,
routes, metadata, and `builder.addModule(...)`.

- `DataModule` registers a fake in-memory service.
- `UsersModule` declares `.dependsOn("data")`.
- module services and routes are applied by `builder.addModule(...)` during `builder.build()`.
- module-created routes and services appear in bootstrap debug metadata.

What to inspect:

- import from the checked-in bootstrap stdlib source path;
- create modules with dependencies, services, routes, and metadata;
- add modules to a builder;
- inspect routes with `app.__getRoutes()`;
- inspect module debug metadata with `app.__debug().modules`.

Current product state:

- This source-stdlib example is a checked-in API-shape fixture.
- `sloppy run --artifacts` currently runs emitted artifacts such as
  `examples/compiler-hello`.
- `sloppyc` compilation and `app.plan.json` emission for this module shape are future
  source-extraction work.
- The bounded `sloppy run` path currently loads generated artifacts, not this
  source-stdlib module example.
- The `data` module is an in-memory service example. Real providers are covered by the
  provider examples and data API tests.
- Module package loading and native plugins belong to later module/package work.
- Bare `"sloppy"` imports are the current source shape for this example.
