# npm Runtime Behavior Gauntlet

This directory holds fixtures that exercise the Sloppy runtime against
realistic pure-JS npm package patterns **after** packaging. Where the
[npm package compatibility gauntlet](../npm-compat/README.md) proves that
package *shapes* resolve, this gauntlet proves that the resolved packages
*execute* correctly when bundled, packaged, copied outside the source
checkout, and run again.

## Scope

- CommonJS module semantics (callable default exports and named export mutation).
- ESM/CJS interop at runtime (default and, where supported, named imports
  across module systems).
- `node:module` `createRequire` and `require.resolve` against the sealed
  compile-time module graph.
- `__dirname` / `__filename` based on sealed module IDs.
- `package.json` data, package self-reference, `imports` aliases, and the
  `import`/`require` conditional branches of `exports`.
- Reading package-bundled assets at runtime.
- Selected `node:` builtin behavior: `Buffer`, `crypto`, `stream`, `zlib`,
  and the synchronous `fs` subset that is backed by the sealed package
  asset policy.

## Out of scope

- Full Node runtime compatibility.
- Native (`.node`) addon execution or N-API.
- Registry install or semver resolution.
- `child_process`, `worker_threads`, `vm`, `inspector`, `repl`, `test`,
  `net`, `tls`, `dns`, `http2`, raw TCP/UDP sockets, full HTTP server
  compatibility.
- Sync `fs` paths that escape the sealed package asset policy.
- Stream backpressure, Transform streams, or `pipeline` variadic forms
  beyond the documented subset.

## Layout

```text
tests/fixtures/npm-runtime/
  matrix.json                 -- machine-readable runtime matrix
  <fixture>/                  -- one directory per matrix entry
    sloppy.json               -- Program Mode app config
    src/main.ts               -- entry point that exercises the package
    node_modules/<pkg>/       -- inline pure-JS dependency
```

Each fixture is self-contained:

1. `sloppy build` produces `.sloppy/{app.js,app.plan.json,deps.graph.json}`.
2. `sloppy package` produces `.sloppy/package/{manifest.json,artifacts/...}`
   and contains no `node_modules/` directories anywhere.
3. The package directory is copied to a sibling outside the checkout and
   re-run under V8. The packaged stdout must match the matrix entry's
   `expectedStdout`.

When V8 is disabled the test still asserts the relocatable artifact
shape; runtime execution is gated on `SLOPPY_EXPECT_RUN_SUCCESS`.

## Status values

| Status        | Meaning |
| ---           | --- |
| `supported`   | Sloppy builds, packages, and the packaged app runs and produces the documented stdout. |
| `partial`     | Sloppy builds and packages cleanly; a documented subset of behavior executes, the rest fails with an explicit runtime error. |
| `stubbed`     | Sloppy resolves the import and the shim is importable but throws a documented error code on use. The fixture asserts the error code. |
| `negative-build`   | Sloppy must emit a documented compile-time diagnostic and fail `sloppy build`. |
| `negative-runtime` | Sloppy builds and packages cleanly; runtime execution must fail with a documented error code on stderr. |

The runtime matrix is the regression baseline for currently exercised
runtime behaviors. Behaviors outside the matrix are not implicit
non-regressions for users; add a new runtime fixture before claiming
support for a new runtime behavior.

## Relationship to the resolver matrix

The resolver matrix (`tests/fixtures/npm-compat/matrix.json`) proves that
package *shapes* resolve at compile time. The runtime matrix
(`tests/fixtures/npm-runtime/matrix.json`) proves that selected package
*behaviors* execute correctly after packaging. The two matrices are
complementary; neither implies the other.
