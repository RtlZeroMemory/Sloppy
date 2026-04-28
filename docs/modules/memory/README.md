# Memory Module

## Status

Planned / not implemented yet.

## Purpose

Define allocation, arena, buffer, and lifetime primitives for safe C runtime work.

## Scope

`SlArena`, allocator interfaces, `SlBuf`, `SlStringBuilder`, and documented lifetimes.

## Non-goals

No arena-everything architecture and no independently closable resource storage only in
arenas.

## Public/Internal API

Planned headers include allocator, arena, buffer, and string builder APIs.

## Ownership/Lifetime Rules

All allocation and view lifetimes must be explicit. Async-crossing data must not point into
shorter-lived arenas.

## Invariants

Allocation sizes use checked math. Raw allocation is isolated to allocator modules once
they exist.

## Diagnostics

Out-of-memory and overflow behavior must be deterministic and avoid recursive allocation.

## Tests

Alignment, overflow rejection, mark/reset, OOM behavior, debug poisoning, and high-water
stats where implemented.

## Source Docs

- `docs/memory.md`;
- `docs/c-standards.md`;
- `docs/testing-strategy.md`;
- ADR 0006.

## Open Questions

- Exact allocator vtable shape.
