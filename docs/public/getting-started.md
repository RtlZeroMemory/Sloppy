# Getting Started

Status: pre-alpha skeleton. This page documents the current supported development path; it
is not a public alpha quickstart.

## Current Runnable Path

The compiler-owned example at `examples/compiler-hello/` is the canonical source input for
the current artifact path.

```powershell
sloppyc build examples/compiler-hello/app.js --out .sloppy-main-smoke
sloppy run --artifacts .sloppy-main-smoke --once GET /
```

When using the Rust toolchain directly:

```powershell
cargo run --manifest-path compiler/Cargo.toml -- build examples/compiler-hello/app.js --out .sloppy-main-smoke
.\build\windows-relwithdebinfo\sloppy.exe run --artifacts .sloppy-main-smoke --once GET /
```

`sloppy run <source.js>` also exists as a development shortcut: it invokes `sloppyc build`,
writes generated artifacts to the documented source-input output location, validates those
artifacts, then runs the artifact path. Runtime execution requires a V8-enabled build.

```powershell
.\build\windows-relwithdebinfo\sloppy.exe run examples/compiler-hello/app.js --once GET /
```

## API Shape Examples

Some examples show the intended application API shape before the compiler/runtime can
execute the full shape:

- `examples/hello/`
- `examples/ergonomics/`
- `examples/modules-basic/`
- `examples/data-foundation/`
- provider examples under `examples/*-basic/`

Those examples are useful for API and static fixture review. They are not all runnable
through the artifact runtime.

## Current Limits

- The runnable path is a bounded development runtime, not a production server.
- V8-gated execution must be reported separately from default non-V8 evidence.
- No HTTPS/TLS, middleware, streaming public API, hot reload, package-manager behavior, or
  Node/Bun/Deno compatibility is claimed.
- Framework v2 ergonomics and final public tutorials are not complete.
- Package smoke is layout evidence, not release readiness.

Related internal docs: `docs/architecture.md`, `docs/execution-model.md`,
`docs/developer-ergonomics.md`, `docs/compiler-supported-syntax.md`.
