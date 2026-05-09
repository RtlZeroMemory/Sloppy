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

Current limitations:

- this source-stdlib example is not executed with `sloppy run --artifacts`;
- `sloppyc` does not compile this example yet;
- `app.plan.json` is not emitted for this example;
- the current bounded `sloppy run` path does not load this source-stdlib module example;
- the `data` module is not a real data provider;
- module package loading and native plugins are outside this example;
- bare `"sloppy"` imports are the current source shape for this example.
