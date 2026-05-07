# V8 Engine Module

## Purpose

The V8 engine module implements Sloppy's optional JavaScript execution backend behind an
engine-neutral C ABI.

## Current Status

When the V8 SDK is resolved and V8 is enabled, the bridge supports classic generated
scripts, registered handler execution, request-context materialization, `Results.*`
descriptor conversion, bounded direct Promise settlement, exception diagnostics,
source-map primary-span remapping, runtime feature-gated intrinsics, and the current
synchronous SQLite bridge.

Default non-V8 gates do not prove this behavior.

## Invariants

- V8 types stay under `src/engine/v8/`.
- Public headers and core modules expose no V8 handles or values.
- One owner thread enters an isolate/context.
- Wrong-thread entry fails before touching V8 state.
- JS never receives raw native pointers.
- Returned strings/results are copied out before crossing the C ABI.
- Pending or rejected Promises fail deterministically; they are not serialized as success.

## Non-Claims

The current bridge is not a Node runtime, npm package loader, general ESM module cache,
browser-compatible JavaScript environment, production async runtime, or broad provider
bridge.

## Tests

V8 tests are a separate evidence lane. V8-gated evidence must name the resolver, preset,
build, and test command used.
