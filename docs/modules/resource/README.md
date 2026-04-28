# Resource Module

## Status

Partially implemented for TASK 05.B.

## Purpose

Represent native resource lifetime building blocks without exposing raw native pointers to
JavaScript.

## Scope

Implemented now:

- `SlScope`: a native cleanup-registration scope for deterministic close-time cleanup.

Future resource work still includes resource IDs, generation counters, kind validation,
close/reuse behavior, and leak checks.

## Non-goals

No real file, database, socket, request-scope, async cancellation, DB transaction,
resource-table integration, JS-visible resource management, V8 cleanup, or OS-handle
behavior in TASK 05.B.

## Public/Internal API

Implemented public header:

- `include/sloppy/scope.h`

Implemented API:

- `sl_scope_init`;
- `sl_scope_init_from_arena`;
- `sl_scope_add_cleanup`;
- `sl_scope_close`;
- `sl_scope_reset`;
- `sl_scope_cleanup_count`;
- `sl_scope_cleanup_capacity`;
- `sl_scope_is_closed`.

Planned future APIs:

- `SlResourceId`;
- resource table insert/get/close APIs.

## Ownership/Lifetime Rules

`SlScope` owns cleanup registrations only. It does not own callback payloads or user data.
Payload and user pointers are borrowed by the scope and may be NULL.

By default, cleanup storage is caller-owned. `sl_scope_init_from_arena` may allocate the
registration storage from a caller-provided `SlArena`; that storage remains valid only
until the arena resets, resets to a mark before the allocation, or its backing buffer ends.

Future resources are expected to be table-owned and referenced by generation-checked IDs.

## Invariants

Implemented scope invariants:

- callbacks are registered in insertion order;
- `sl_scope_close` invokes callbacks in reverse registration order;
- each registered callback is invoked at most once by close;
- close is idempotent and a second close returns OK without invoking callbacks;
- registration after close fails with `SL_STATUS_INVALID_STATE`;
- storage exhaustion fails with `SL_STATUS_CAPACITY_EXCEEDED` and does not mutate the
  existing registrations;
- `sl_scope_reset` clears registrations without invoking callbacks and marks the scope open
  for reuse.

Future resource-table invariants still include stale-ID and wrong-kind failure.

## Diagnostics

Scope APIs return `SlStatus` only and do not emit human diagnostics. Invalid arguments,
invalid state, and capacity exhaustion are machine-readable status results. Wrong-kind,
stale-ID, double-close, and leak diagnostics are expected as the resource table grows.

## Tests

CTest registers `tests/unit/core/test_scope.c`, covering:

- initialization, including zero-capacity scopes;
- cleanup registration and count/capacity helpers;
- NULL cleanup function rejection;
- payload/user pointer forwarding, including NULL user values;
- capacity exhaustion and failed-registration atomicity;
- registration after close;
- LIFO close order and exact-once close behavior;
- idempotent double close and close on empty scopes;
- reset clearing registrations without invoking callbacks;
- arena-backed cleanup storage initialization.

Future tests still need stale ID, wrong kind, close/reuse, leak reporting, and generation
counter behavior for the resource table.

## Source Docs

- `docs/memory.md`;
- `docs/concurrency.md`;
- `docs/architecture.md`;
- `docs/testing-strategy.md`;
- ADR 0006.

## Open Questions

- Exact resource ID layout.
- Exact request-scope, async cancellation, and resource-table integration policy remains
  future EPIC work.
