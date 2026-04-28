# Engine Boundary

Engine adapters live under this directory. Runtime-facing interfaces must remain C-shaped
and must not expose concrete engine implementation types.

`src/engine/engine.c` implements the current engine-neutral C ABI from
`include/sloppy/engine.h`. It provides only an arena-backed noop engine and deterministic
unsupported behavior for handler execution and V8 creation.

The first planned real backend is V8 through `src/engine/v8/`. Other engines may be
evaluated later only if the boundary remains intact.
