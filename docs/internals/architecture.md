# Architecture Internals

## Where It Lives

- `src/core/**`
- `src/data/**`
- `src/engine/**`
- `src/platform/**`
- `include/sloppy/*.h`
- `compiler/src/**`
- `stdlib/sloppy/**`

## Shape

Sloppy has a native runtime kernel, an isolated V8 bridge, a Rust compiler, and
a JavaScript stdlib/bootstrap layer. The compiler emits artifacts. The runtime
validates the Plan and dispatches into V8 only in V8-enabled builds.

## Invariants

- Core modules do not include OS headers.
- Platform-specific APIs stay under `src/platform/*`.
- V8 types stay under `src/engine/v8/*`.
- FFI boundaries report Sloppy diagnostics instead of leaking exceptions or
  panics.
