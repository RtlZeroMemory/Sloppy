# 0011: Compiler and Execution Model

## Status

Accepted.

## Context

Sloppy executes TypeScript apps, but V8 executes JavaScript. The runtime needs a native app
graph and handler IDs. Development and production paths must share the same architecture.
Diagnostics require source maps. The runtime and compiler need a stable artifact contract.

## Decision

`sloppyc` emits `app.js`, `app.js.map`, and `app.plan.json`. `app.plan.json` is the host
graph authority. `app.js` contains executable handler functions.

The runtime validates the plan before user code, loads JavaScript through the V8 bridge,
dispatches handlers by numeric IDs, and verifies plan/bundle handler consistency.

`sloppy run` and `sloppy build` share the compiler pipeline.

## Consequences

Sloppy needs more upfront compiler/runtime boundary work. In return, it gets better
diagnostics, performance, audit, packaging, and production parity.

Dynamic behavior must be explicit and less optimized. Sloppy is not a raw eval-style
runtime.

## Alternatives Considered

- Direct TypeScript eval: rejected because V8 executes JavaScript and diagnostics/build
  artifacts would be weak.
- Runtime-only route discovery: rejected because the native host needs a validated graph.
- Node-style dynamic framework registration only: rejected because hidden runtime mutation
  weakens plan validation.
- Compiler-only registration with no JS startup verification: rejected because plan/bundle
  drift must be caught.
- Separate dev and production execution models: rejected because it creates fake parity.

## Follow-up Tasks

- Implement handwritten `app.js` plus `app.plan.json` execution before compiler extraction.
- Add source map diagnostic fixtures before generated JS is user-facing.
- Add plan/bundle consistency checks to startup.
- Ensure `sloppy run` and `sloppy build` share artifacts.
