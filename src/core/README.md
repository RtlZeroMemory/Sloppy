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
- small `SlCancellationToken` snapshot primitive for cancelled/deadline/shutdown/
  backpressure request and native operation boundaries.
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
  arena-copying builder, deterministic text/JSON renderers, redaction helpers, and stable
  diagnostic code-name coverage.
- app-host lifecycle, runtime feature, capability, filesystem, OS, crypto, network, HTTP,
  provider-executor, async backend, worker-pool, and resource/leak foundations where their
  ownership is documented under `docs/modules/` or `docs/project/`.

Keep new core modules bounded: ownership, tests, diagnostics, and public headers must be
specified before adding production behavior. Test-only hooks must stay narrow and must not
be presented as public API.
