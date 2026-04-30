# Memory Module

## Status

Partially implemented through ENGINE-21.A/B/C/E/F primitive foundations.

## Purpose

Define allocation, arena, buffer, and lifetime primitives for safe C runtime work.

## Scope

Implemented now:

- `SlArena`: caller-backed scoped arena allocation;
- `SlArenaMark`: offset marks for scoped reset.
- `SlOwnedStr` and `SlOwnedBytes`: ownership markers for arena-copy helpers;
- `SlByteBuilder`: bounded byte builder over caller-owned fixed storage or arena storage;
- `SlStringBuilder`: bounded string builder and small decimal formatting helpers;
- `SlInternTable`: bounded app/static-lifetime string interning for stable metadata;
- optional `SlScope` cleanup registration storage allocated from `SlArena`.

Future memory work still includes allocator interfaces, a standalone heap/operation-owned
`SlBuf`, ENGINE-21.D V8/SQLite conversion policy, and ENGINE-22 subsystem adoption.

## Non-goals

No arena-everything architecture and no independently closable resource storage only in
arenas. Current arena work does not add an OS page allocator, malloc-backed arena factory,
scratch arena system, request lifecycle, resource table, or generic allocator framework.

## Public/Internal API

Implemented public headers:

- `include/sloppy/arena.h`
- `include/sloppy/string.h`
- `include/sloppy/bytes.h`
- `include/sloppy/builder.h`
- `include/sloppy/intern.h`

Implemented API:

- `sl_arena_init`;
- `sl_arena_reset`;
- `sl_arena_mark`;
- `sl_arena_reset_to`;
- `sl_arena_alloc`;
- `sl_arena_dispose`;
- `sl_arena_capacity`;
- `sl_arena_used`;
- `sl_arena_remaining`;
- `sl_arena_high_water`.

String/byte helpers add deterministic hash functions, arena-copy helpers, suffix helpers,
and owned-view adapters. String/byte views remain borrowed pointer-plus-length values; the
owned types describe the producer lifetime but do not free memory.

Builders preserve their already-written prefix after failed append/reserve calls. Fixed
builders report `SL_STATUS_CAPACITY_EXCEEDED` when caller storage is exhausted. Arena
builders grow by allocating replacement buffers from a caller-supplied arena up to the
explicit maximum capacity.

Intern tables copy stable metadata strings into the caller-supplied app/static arena,
store hash/length metadata, resolve bucket collisions with byte equality, and return
generation-tagged `SlSymbol` values. Interning request bodies, secrets, connection strings,
arbitrary user payloads, or transient diagnostics is forbidden by policy.

`sl_arena_alloc` requires a nonzero size and a nonzero power-of-two alignment. Zero-size
allocation is rejected as `SL_STATUS_INVALID_ARGUMENT` so callers do not depend on an
ambiguous pointer for an allocation that owns no bytes.

`SlScope` lives in the resource module, but `sl_scope_init_from_arena` may use `SlArena` to
allocate fixed cleanup-registration storage. The scope still owns only registrations; it
does not make callback payloads arena-owned.

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

Arena-backed scope storage has the same lifetime as any other arena allocation. Resetting
the arena invalidates that storage; callers must close or stop using the scope before that
happens.

Arena-owned copied strings, copied bytes, builder buffers, intern table entries, and
interned string bytes follow the same invalidation rules. Request-owned memory must not
escape request cleanup unless it is copied into an owner that outlives the async operation.

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
- dispose invalidation without freeing caller-owned backing storage.

`tests/unit/core/test_scope.c` covers the arena-backed scope-storage helper.
`tests/unit/core/test_string.c` and `tests/unit/core/test_bytes.c` cover view helpers,
hashing, arena copies, embedded NUL/binary data, and unchanged outputs on failure.
`tests/unit/core/test_builder.c` covers fixed and arena builders, growth, reserve,
formatting, optional NUL views, capacity failure, and prefix preservation after failure.
`tests/unit/core/test_intern.c` covers duplicate interning, bucket collisions, capacity
failure, stale symbols, and unchanged outputs.

## Source Docs

- `docs/memory.md`;
- `docs/c-standards.md`;
- `docs/testing-strategy.md`;
- ADR 0006.

## Open Questions

- Exact allocator vtable shape.
- Exact OS page allocation API remains future platform-abstraction work.
- Exact V8/native and SQLite text/blob helper surface remains ENGINE-21.D follow-up.
