# Compiler Hello Example

Status: compiler extraction MVP input.

This example is the first Sloppy source shape that `sloppyc build` can compile:

```powershell
cargo run --manifest-path compiler/Cargo.toml -- build examples/compiler-hello/app.js --out .sloppy
```

The command emits:

- `.sloppy/app.plan.json`;
- `.sloppy/app.js`;
- `.sloppy/app.js.map`.

The generated artifacts are intended for the EPIC-22 `sloppy run` path. This example does
not start an HTTP server, does not use Node/npm/package-manager behavior, and does not
claim full TypeScript checking or broad bundling.
