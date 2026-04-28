# Core Module

## Status

Partially implemented for TASK 02.A and TASK 03.A.

## Purpose

Portable runtime core primitives such as status, source locations, string/byte views,
checked math, assertions, the first caller-backed arena primitive, the first native cleanup
scope primitive, the minimal plan contract helpers, and the first diagnostics foundation.

## Scope

Core C primitives that other runtime modules can depend on.

## Non-goals

No V8, HTTP, database, OS API, or app-host feature behavior.

## Public/Internal API

Implemented public C headers under `include/sloppy/`:

- `status.h`: small machine-readable `SlStatus`/`SlStatusCode` result values;
- `source_loc.h`: borrowed C call-site metadata and `SL_SOURCE_LOC_CURRENT`;
- `string.h`: borrowed `SlStr` pointer-plus-length views;
- `bytes.h`: borrowed `SlBytes` pointer-plus-length views;
- `checked_math.h`: checked `size_t` add/multiply helpers;
- `assert.h`: internal invariant assertion macros;
- `arena.h`: caller-backed `SlArena` allocation, marks, resets, and usage stats;
- `scope.h`: caller-backed or arena-backed cleanup registration scopes with deterministic
  LIFO close behavior;
- `plan.h`: minimal Plan v1 native structs, supported version checks, handler ID rules,
  handler lookup, and duplicate handler ID detection;
- `diagnostics.h`: diagnostic severity/code model, user/app source spans, bounded related
  spans and hints, arena-copying builder, and deterministic text renderer.

Implementations live under `src/core/`.

## Ownership/Lifetime Rules

`SlStatus` owns no memory and is passed by value.

`SlSourceLoc` file/function strings are borrowed compiler-provided strings when captured by
`SL_SOURCE_LOC_CURRENT`; callers do not free them.

`SlStr` and `SlBytes` are borrowed views. They do not allocate, copy, free, transfer
ownership, require NUL termination, or validate encoding. Non-empty views require the
caller to keep the backing memory valid for the documented lifetime.

Checked math output pointers are caller-owned and required. Failed checked math calls leave
the output storage unchanged.

`SlArena` does not own its backing buffer. Arena allocations are arena-owned scoped memory
valid until reset/reset-to-mark or backing buffer end. Arena memory is not suitable for
independently closable resources and must not cross async boundaries unless the arena
outlives the async operation.

`SlScope` owns cleanup registrations only. Cleanup payload and user pointers are borrowed
and may be NULL. Scope cleanup storage is caller-owned unless allocated through
`sl_scope_init_from_arena`, in which case the storage lifetime follows the arena allocation.

`SlPlan` and its child structs own no memory. Plan strings are borrowed `SlStr` views, and
the handler table is a borrowed caller-owned array.

## Invariants

Core code stays portable C17 and does not include OS or V8 headers.

Core primitives avoid runtime features, platform APIs, V8, package-manager behavior, and
speculative future abstractions. `SlArena` performs pointer-bump allocation only inside a
caller-provided buffer. `SlScope` performs fixed-capacity cleanup registration only inside
caller-provided or arena-provided storage. Plan helpers do not parse JSON, validate files,
or build runtime host graphs.

## Diagnostics

Low-level status is separate from human diagnostics.

`SlStatus` carries only a stable status code. EPIC-04 now provides the initial `SlDiag`
model and plain-text renderer for human-facing diagnostics. Source frames, JSON output, and
runtime integrations remain future diagnostics work.

TASK 05.B adds two narrow status codes:

- `SL_STATUS_INVALID_STATE` for valid arguments used against the wrong object state, such as
  registering cleanup after a scope has closed;
- `SL_STATUS_CAPACITY_EXCEEDED` for fixed caller-provided storage exhaustion.

## Tests

CTest now registers focused C unit tests for status, source locations, string views, byte
views, checked size arithmetic, arena behavior, scope cleanup lifetime behavior, minimal
plan contract helper behavior, diagnostics foundation behavior, and assertion macro
compilation.

## Source Docs

- `docs/c-standards.md`;
- `docs/c-style.md`;
- `docs/memory.md`;
- `docs/testing-strategy.md`;
- `docs/testing.md`;
- `docs/quality-gates.md`.

## Open Questions

- Whether to vendor munit later, once the project wants richer C test reporting.
