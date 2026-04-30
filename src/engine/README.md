# Engine Boundary

Engine adapters live under this directory. Runtime-facing interfaces must remain C-shaped
and must not expose concrete engine implementation types.

`src/engine/engine.c` implements the engine-neutral C ABI from `include/sloppy/engine.h`.
It provides the arena-backed noop engine, deterministic unsupported behavior when V8 is not
compiled in, and dispatches to the optional V8 backend when the build is configured with a
valid SDK.

The current real backend is V8 through `src/engine/v8/`. V8 types stay in that subtree,
including Promise settlement, explicit microtask drains, owner-thread checks, result
conversion, and provider intrinsic modules. Other engines may be evaluated later only if
the boundary remains intact.
