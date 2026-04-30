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
    runtime-classic.js
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
SQLite/PostgreSQL/SQL Server provider metadata. `data.sqlite("main")` and
`data.sqlite.open(...)` return safe SQLite wrappers only when the V8 runtime installs the
native SQLite bridge and passes Plan/capability metadata into the engine; in bootstrap-only
or non-V8 contexts they report bridge-unavailable. In V8 contexts with that bridge
installed, denied SQLite operations fail with capability-access errors rather than
bridge-unavailable errors. `data.postgres.open(...)` and
`data.sqlserver.open(...)` are future stdlib entry points for native providers and still
report bridge-unavailable until their own bridge modules exist.
`Sloppy.createBuilder()` exposes minimal config, logging, capabilities, and services builders;
`Sloppy.module(...)` creates bootstrap app module definitions; `builder.addModule(...)`
registers them; `builder.build()` freezes builder mutation and returns an in-memory app
object. `Sloppy.create()` remains supported as a default builder plus `build()`.

The current app object supports `app.mapGet(...)`, `app.mapGroup(...)`, route registration
storage, route metadata storage, `.withName(...)`, structural `app.freeze()`,
`app.isFrozen()`, `app.config`, `app.log`, `app.capabilities`, `app.services`,
`app.__getRoutes()`, and
`app.__debug().modules` for bootstrap tests/debugging.

`internal/runtime-classic.js` is the EPIC-24 V8-gated bridge asset for generated artifacts.
It is not the public ESM stdlib and it is not a Node compatibility shim. `sloppy run`
loads this classic script from the staged bootstrap stdlib root before evaluating generated
`app.js`; generated code reads `globalThis.__sloppy_runtime.Results` and registers
handlers through the V8-installed `__sloppy_register_handler(id, handler)` intrinsic.

Not implemented here:

- `app.run`, `app.listen`, or `app.build`;
- public handler registration APIs;
- compiler extraction or `app.plan.json` emission;
- HTTP server behavior or response writing;
- JavaScript-to-native PostgreSQL/SQL Server provider calls;
- database connections or SQL execution from JavaScript outside the V8-gated,
  capability-wired SQLite bridge;
- nested route groups, middleware, automatic validation/request binding, module package
  loading, or native plugins;
- config files, environment variables, command-line config, or secret managers;
- console, file, or native logging sinks;
- request-scoped service lifetimes, disposal hooks, async factories, or typed tokens;
- broad runtime intrinsic binding;
- Node/npm module resolution, compiler module extraction, or arbitrary import rewriting.
