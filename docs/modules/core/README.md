# Core Module

## Status

Partially implemented for TASK 02.A and TASK 03.A.

## Purpose

Portable runtime core primitives such as status, source locations, string/byte views,
checked math, assertions, and the first caller-backed arena primitive.

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
- `arena.h`: caller-backed `SlArena` allocation, marks, resets, and usage stats.

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

## Invariants

Core code stays portable C17 and does not include OS or V8 headers.

Core primitives avoid runtime features, platform APIs, V8, package-manager behavior, and
speculative future abstractions. `SlArena` performs pointer-bump allocation only inside a
caller-provided buffer.

## Diagnostics

Low-level status is separate from human diagnostics.

`SlStatus` carries only a stable status code. Diagnostics rendering, source frames, and
human-facing messages are future EPIC-04 work.

## Tests

CTest now registers focused C unit tests for status, source locations, string views, byte
views, checked size arithmetic, arena behavior, and assertion macro compilation.

## Source Docs

- `docs/c-standards.md`;
- `docs/c-style.md`;
- `docs/memory.md`;
- `docs/testing-strategy.md`;
- `docs/testing.md`;
- `docs/quality-gates.md`.

## Open Questions

- Whether to vendor munit later, once the project wants richer C test reporting.
