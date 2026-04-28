# Resource Module

## Status

Planned / not implemented yet.

## Purpose

Represent JS-visible native resources without exposing raw native pointers.

## Scope

Resource IDs, generation counters, kind validation, close/reuse behavior, and leak checks.

## Non-goals

No real file, database, socket, or V8 resource implementation in the foundation pass.

## Public/Internal API

Planned `SlResourceId` and resource table APIs.

## Ownership/Lifetime Rules

Resources are table-owned and referenced by generation-checked IDs.

## Invariants

Stale IDs and wrong-kind access must fail predictably.

## Diagnostics

Wrong-kind, stale-ID, double-close, and leak diagnostics are expected as the module grows.

## Tests

Stale ID, wrong kind, close/reuse, leak reporting, and generation counter behavior.

## Source Docs

- `docs/memory.md`;
- `docs/architecture.md`;
- `docs/testing-strategy.md`;
- ADR 0006.

## Open Questions

- Exact resource ID layout.
