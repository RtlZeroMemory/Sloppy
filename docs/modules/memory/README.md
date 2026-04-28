# Memory Module

## Status

Partially implemented for TASK 03.A.

## Purpose

Define allocation, arena, buffer, and lifetime primitives for safe C runtime work.

## Scope

Implemented now:

- `SlArena`: caller-backed scoped arena allocation;
- `SlArenaMark`: offset marks for scoped reset.

Future memory work still includes allocator interfaces, `SlBuf`, and `SlStringBuilder`.

## Non-goals

No arena-everything architecture and no independently closable resource storage only in
arenas. TASK 03.A does not add an OS page allocator, malloc-backed arena factory, scratch
arena system, request lifecycle, resource table, or generic allocator framework.

## Public/Internal API

Implemented public header:

- `include/sloppy/arena.h`

Implemented API:

- `sl_arena_init`;
- `sl_arena_reset`;
- `sl_arena_mark`;
- `sl_arena_reset_to`;
- `sl_arena_alloc`;
- `sl_arena_capacity`;
- `sl_arena_used`;
- `sl_arena_remaining`;
- `sl_arena_high_water`.

`sl_arena_alloc` requires a nonzero size and a nonzero power-of-two alignment. Zero-size
allocation is rejected as `SL_STATUS_INVALID_ARGUMENT` so callers do not depend on an
ambiguous pointer for an allocation that owns no bytes.

## Ownership/Lifetime Rules

`SlArena` does not own its backing buffer. The caller owns the buffer lifetime and must keep
it alive while the arena or arena allocations are used.

Arena allocations remain valid until:

- `sl_arena_reset` invalidates all allocations;
- `sl_arena_reset_to` invalidates allocations above the mark;
- the caller-owned backing buffer lifetime ends.

Arena allocations must not be used for independently closable resources. Data crossing
async boundaries must not point into a shorter-lived arena. `SlArena` is not thread-safe
unless callers provide external synchronization.

## Invariants

Allocation sizes and arena cursor movement use checked size arithmetic. Returned pointers
must satisfy the requested power-of-two alignment. The arena never calls OS APIs or raw
allocation APIs.

High-water usage records the largest offset reached and does not shrink on reset.

Arena marks carry a generation counter in all builds. A full reset increments the generation
and rejects old marks in later `sl_arena_reset_to` calls. Marks that point beyond the
current used bytes or capacity are also rejected.

Assert-enabled builds poison allocated bytes and reset ranges with recognizable byte
patterns. This is a lightweight stale-use aid, not ASan integration, guard pages,
quarantine, or a debug allocator.

## Diagnostics

Out-of-memory and overflow behavior is deterministic and avoids recursive allocation.
Low-level arena functions return `SlStatus` only; human diagnostics remain future work.

## Tests

CTest registers `tests/unit/core/test_arena.c`, covering:

- valid and invalid initialization;
- zero-capacity arenas;
- simple and repeated allocation;
- alignment for 1, 2, 4, 8, 16, and `_Alignof(max_align_t)`;
- zero-size and invalid-alignment rejection;
- capacity failure and overflow-safe failure paths;
- mark/reset, nested marks, future marks, null marks, full reset, and stale mark behavior
  in all builds;
- capacity, used, remaining, and high-water stats;
- debug poisoning when `SL_ENABLE_ASSERTS` is enabled.

## Source Docs

- `docs/memory.md`;
- `docs/c-standards.md`;
- `docs/testing-strategy.md`;
- ADR 0006.

## Open Questions

- Exact allocator vtable shape.
- Exact OS page allocation API remains future platform-abstraction work.
