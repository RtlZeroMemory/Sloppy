# Runtime Core

The C runtime core owns portable foundation code that other runtime modules can depend on.

Implemented TASK 02.A primitives:

- `SlStatus`/`SlStatusCode`;
- `SlSourceLoc`;
- borrowed `SlStr` and `SlBytes` views;
- checked `size_t` add/multiply helpers;
- internal assertion macros;
- caller-backed `SlArena` allocation, mark/reset, and high-water stats.
- fixed-capacity `SlResourceTable` with JS-safe slot/generation IDs, kind validation,
  stale-handle diagnostics, deterministic close/reuse, and cleanup callbacks.
- caller-backed `SlLoop` native completion queue skeleton.
- caller-owned `SlAsync` native promise settlement skeleton over `SlLoop`.
- inline/fake `SlWorkerPool` native worker completion skeleton over `SlLoop`.
- complete-buffer HTTP/1 request-head parser over llhttp plus a libuv dependency smoke.
- native route pattern parser and one-pattern matcher for static segments, string params,
  int params, and parameter captures.
- synthetic in-memory GET dispatch from parsed HTTP request head through manual route
  bindings to numeric Sloppy Plan handler IDs and the existing runtime-contract/engine
  boundary.
- TASK 06.A minimal Plan v1 structs/helpers for version support, handler ID rules, handler
  lookup, and duplicate ID detection.
- TASK 04.A diagnostic severity/code model, source spans, bounded related spans/hints,
  arena-copying builder, and deterministic text renderer.

No app-host runtime feature code exists here yet. Add modules only after their ownership,
tests, diagnostics, and public headers are specified.
