# Sloppy Bootstrap Stdlib

Status: Bootstrap app-host skeleton.

This directory is the source-controlled bootstrap standard library layout for the future
public Sloppy TypeScript API facade.

Source layout:

```text
stdlib/sloppy/
  index.js
  results.js
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

The current modules implement the first app-host foundation facade. `index.js` re-exports
frozen `Sloppy` and `Results` objects. `Results.text(...)` and `Results.json(...)` return
plain frozen descriptor objects. `Sloppy.createBuilder()` exposes minimal config, logging,
and services builders; `builder.build()` freezes builder mutation and returns an in-memory
app object. `Sloppy.create()` remains supported as a default builder plus `build()`.

The current app object supports `app.mapGet(...)`, route registration storage,
`.withName(...)`, structural `app.freeze()`, `app.isFrozen()`, `app.config`, `app.log`,
`app.services`, and `app.__getRoutes()` for bootstrap tests/debugging.

Not implemented here:

- `app.run`, `app.listen`, or `app.build`;
- handler registration;
- compiler extraction or `app.plan.json` emission;
- HTTP server behavior or response writing;
- route groups, middleware, validation, or modules;
- config files, environment variables, command-line config, or secret managers;
- console, file, or native logging sinks;
- request-scoped service lifetimes, disposal hooks, async factories, or typed tokens;
- runtime intrinsic binding;
- module resolution or compiler import rewriting.
