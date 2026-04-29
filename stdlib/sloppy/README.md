# Sloppy Bootstrap Stdlib

Status: Bootstrap API shape.

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

The current modules implement the first tiny public API facade. `index.js` re-exports
frozen `Sloppy` and `Results` objects. `Results.text(...)` and `Results.json(...)` return
plain frozen descriptor objects. `Sloppy.create()` returns an in-memory app object with
`app.mapGet(...)`, route registration storage, `.withName(...)`, and `app.__getRoutes()`
for bootstrap tests/debugging.

Not implemented here:

- `app.run`, `app.listen`, `app.build`, or app graph freeze;
- handler registration;
- compiler extraction or `app.plan.json` emission;
- HTTP server behavior or response writing;
- route groups, middleware, validation, modules, services, config, or logging;
- runtime intrinsic binding;
- module resolution or compiler import rewriting.
