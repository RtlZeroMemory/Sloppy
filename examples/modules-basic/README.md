# Modules Basic Example

Status: Bootstrap module skeleton example.

This example shows the current JavaScript-only `Sloppy.module(...)` API shape:

- `DataModule` registers a fake in-memory service.
- `UsersModule` declares `.dependsOn("data")`.
- module services and routes are applied by `builder.addModule(...)` during `builder.build()`.
- module-created routes and services appear in bootstrap debug metadata.

What works today:

- import from the checked-in bootstrap stdlib source path;
- create modules with dependencies, services, routes, and metadata;
- add modules to a builder;
- inspect routes with `app.__getRoutes()`;
- inspect module debug metadata with `app.__debug().modules`.

What does not work yet:

- this source-stdlib example is not a `sloppy run --artifacts` app;
- `sloppyc` does not compile this example;
- this example does not emit `app.plan.json`;
- the current bounded `sloppy run` path does not load this source-stdlib module example;
- the `data` module is not a real data provider;
- module package loading and native plugins are future work;
- the future bare `"sloppy"` import is planned only.
