# Runtime Core

The C runtime core owns portable foundation code that other runtime modules can depend on.

Implemented TASK 02.A primitives:

- `SlStatus`/`SlStatusCode`;
- `SlSourceLoc`;
- borrowed `SlStr` and `SlBytes` views;
- checked `size_t` add/multiply helpers;
- internal assertion macros;
- caller-backed `SlArena` allocation, mark/reset, and high-water stats.
- caller-backed `SlLoop` native completion queue skeleton.
- caller-owned `SlAsync` native promise settlement skeleton over `SlLoop`.
- inline/fake `SlWorkerPool` native worker completion skeleton over `SlLoop`.
- TASK 06.A minimal Plan v1 structs/helpers for version support, handler ID rules, handler
  lookup, and duplicate ID detection.
- TASK 04.A diagnostic severity/code model, source spans, bounded related spans/hints,
  arena-copying builder, and deterministic text renderer.

No app-host runtime feature code exists here yet. Add modules only after their ownership,
tests, diagnostics, and public headers are specified.
