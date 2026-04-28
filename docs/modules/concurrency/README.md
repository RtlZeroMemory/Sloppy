# Concurrency Module

## Status

Planned / not implemented yet.

## Purpose

Define Sloppy's event loop, completion queue, owner-thread, promise settlement, and worker
pool model.

## Scope

`SlLoop`, native completion queue, promise settlement, request lifetime, cancellation,
backpressure, and worker pool boundaries.

## Non-goals

No thread-per-request model and no worker-pool entry into a shared V8 isolate.

## Public/Internal API

Planned loop and worker abstractions stay engine-neutral and platform-safe.

## Ownership/Lifetime Rules

Request scopes live until promise settlement, response completion, or cancellation cleanup.

## Invariants

Native worker threads never call JS directly.

## Diagnostics

Rejected promise, cancellation, overload, and wrong-thread diagnostics later.

## Tests

Completion queue ordering, promise settlement, cancellation cleanup, and no-V8-entry worker
tests later.

## Source Docs

- `docs/concurrency.md`;
- `docs/execution-model.md`;
- `docs/memory.md`;
- `docs/testing-strategy.md`;
- ADR 0014.

## Open Questions

- Exact event loop backend integration.
