# Sloppy Bootstrap Stdlib

Status: Bootstrap layout only.

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

The current modules are placeholders. `index.js` re-exports empty frozen `Sloppy` and
`Results` objects so future compiler and runtime work has a stable module boundary to grow
from. `internal/intrinsics.js` reserves the internal bootstrap boundary for future
runtime-provided intrinsic bindings.

Not implemented here:

- `Results.text` or `Results.json`;
- `Sloppy.create`;
- `app.mapGet`;
- handler registration;
- runtime intrinsic binding;
- module resolution or compiler import rewriting.
