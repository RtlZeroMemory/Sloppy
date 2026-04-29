# Sloppy Bootstrap Stdlib

Status: Bootstrap app-host skeleton.

This directory is the source-controlled bootstrap standard library layout for the future
public Sloppy TypeScript API facade.

Source layout:

```text
stdlib/sloppy/
  index.js
  data.js
  results.js
  schema.js
  app.js
  internal/
    intrinsics.js
  bootstrap.manifest.json
  README.md
```

Build and install layout:

```text
lib/sloppy/bootstrap/sloppy/
```

The current modules implement the first app-host foundation and developer ergonomics
facade. `index.js` re-exports frozen `Sloppy`, `data`, `sql`, `Results`, and `schema`
objects. The implemented `Results.*` helpers return plain frozen descriptor objects.
`schema` exposes a small validation skeleton for string, number, boolean, and object
shapes.
`sql` and `data` expose bootstrap query-template lowering, the fake-provider contract, and
SQLite/PostgreSQL/SQL Server provider metadata. `data.sqlite.open(...)`,
`data.postgres.open(...)`, and `data.sqlserver.open(...)` are future stdlib entry points
for native providers, but they report bridge-unavailable errors until runtime intrinsics and
JS-visible resource IDs exist.
`Sloppy.createBuilder()` exposes minimal config, logging, capabilities, and services builders;
`Sloppy.module(...)` creates bootstrap app module definitions; `builder.addModule(...)`
registers them; `builder.build()` freezes builder mutation and returns an in-memory app
object. `Sloppy.create()` remains supported as a default builder plus `build()`.

The current app object supports `app.mapGet(...)`, `app.mapGroup(...)`, route registration
storage, route metadata storage, `.withName(...)`, structural `app.freeze()`,
`app.isFrozen()`, `app.config`, `app.log`, `app.capabilities`, `app.services`,
`app.__getRoutes()`, and
`app.__debug().modules` for bootstrap tests/debugging.

Not implemented here:

- `app.run`, `app.listen`, or `app.build`;
- handler registration;
- compiler extraction or `app.plan.json` emission;
- HTTP server behavior or response writing;
- JavaScript-to-native SQLite/PostgreSQL/SQL Server provider calls;
- database connections or SQL execution from JavaScript;
- nested route groups, middleware, automatic validation/request binding, module package
  loading, or native plugins;
- config files, environment variables, command-line config, or secret managers;
- console, file, or native logging sinks;
- request-scoped service lifetimes, disposal hooks, async factories, or typed tokens;
- runtime intrinsic binding;
- module resolution, compiler module extraction, or compiler import rewriting.
