# 0003: C Runtime Kernel

## Status

Accepted.

## Context

Sloppy needs native ownership over lifecycle, memory, resources, permissions, diagnostics,
route dispatch, and app hosting. It also needs V8, which is a C++ API, and a Rust compiler
tool for TypeScript processing.

## Decision

The runtime kernel is C17. C++ is allowed only inside the engine bridge, initially under
`src/engine/v8/`. Rust is used for the compiler tool under `compiler/`.

Engine implementation types must not leak into the C runtime. In particular, `v8::*` types
are forbidden outside the V8 bridge.

## Consequences

The core runtime stays small, portable, and explicit about ownership. The engine bridge must
translate between C runtime concepts and V8 concepts at a hard boundary.

## Alternatives Considered

- C++ runtime core: rejected to keep the kernel simpler and stricter.
- Rust runtime core: deferred because the stated runtime target is C.
- V8 types in shared headers: rejected because it would couple the entire host to V8.

## Follow-up Tasks

- Implement core C primitives before runtime features.
- Keep C++ files isolated under `src/engine/v8/`.
- Add checks/reviews for V8 type leakage.
- Keep Rust confined to compiler tooling until an explicit ADR changes that.
