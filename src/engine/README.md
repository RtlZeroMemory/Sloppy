# Engine Boundary

Engine adapters live under this directory. Runtime-facing interfaces must remain C-shaped
and must not expose concrete engine implementation types.

The first planned backend is V8 through `src/engine/v8/`. Other engines may be evaluated
later only if the boundary remains intact.
