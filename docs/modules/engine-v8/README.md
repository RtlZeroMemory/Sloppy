# Engine V8 Module

## Status

Planned / not implemented yet.

## Purpose

Host V8 behind an isolated C++ bridge without leaking V8 types into the C runtime.

## Scope

V8 SDK detection, bridge ABI, isolate/context lifecycle, handler calls, exceptions, and
promise settlement later.

## Non-goals

No HTTP, compiler extraction, or public JS API bootstrap in the smoke phase.

## Public/Internal API

Runtime-visible APIs must be C-shaped and engine-neutral. V8 implementation details stay
under `src/engine/v8/`.

## Ownership/Lifetime Rules

V8 handles are bridge-owned. JS never receives raw native pointers.

## Invariants

One V8 isolate has one owning JS thread.

## Diagnostics

Bridge initialization, thrown exception, rejected promise, and handler mismatch diagnostics.

## Tests

V8 leakage scanner, bridge smoke, thrown exception diagnostic, and wrong-thread checks when
available.

## Source Docs

- `docs/architecture.md`;
- `docs/execution-model.md`;
- `docs/concurrency.md`;
- `docs/testing-strategy.md`;
- ADR 0003;
- ADR 0014.

## Open Questions

- Final SDK artifact layout.
