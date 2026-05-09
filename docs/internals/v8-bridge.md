# V8 Bridge Internals

## Where It Lives

- `src/engine/v8/**`
- `include/sloppy/engine.h`
- `tests/unit/engine/**`

## Invariants

- V8 types are not exposed outside `src/engine/v8/*`.
- Native worker threads must not enter a V8 isolate unless the bridge documents
  ownership.
- JavaScript must not receive raw native pointers.
- C++ exceptions must be mapped to Sloppy diagnostics before crossing ABI
  boundaries.

## Evidence

Default engine tests cover unsupported non-V8 behavior. V8 smoke and bridge
tests are V8-gated.
