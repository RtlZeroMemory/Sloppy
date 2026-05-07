# Core Module

## Purpose

The core module provides Sloppy's low-level C contracts: status values, source locations,
borrowed string/byte views, checked arithmetic, arenas, builders, interned metadata,
diagnostics integration, and small ownership primitives.
Arena and builder stats are read-only internal measurement evidence; they do not introduce
allocator ownership or performance claims.
SIMD files may accelerate canonical byte/string primitives when `SLOPPY_ENABLE_SIMD=AUTO`
detects a supported baseline backend. Advanced tiers such as AVX2 are configured through
`SLOPPY_SIMD_LEVEL`, but the scalar reference path remains the public contract and
fallback. Explicit small builders provide SSO for local construction; arena builders remain
the contract for returned arena-owned views.

## Current Status

Implemented core primitives are used by Plan parsing, diagnostics, HTTP parsing/response
paths, route matching, app-host startup, provider metadata, and selected V8/native bridge
paths.

Core code must remain independent from OS headers, V8 types, JavaScript object models, and
provider-specific implementations.

## Invariants

- Borrowed views are not NUL-terminated contracts.
- NUL-terminated API boundaries must use the canonical no-NUL validation/copy helper rather
  than treating `SlStr` storage as a C string.
- Arena-owned outputs must state the arena lifetime.
- Arena stats snapshots are side-effect-free and must not make stale marks valid.
- Checked arithmetic is required for potentially overflowing sizes or offsets. Array
  allocation counts should use the canonical checked array-size helper.
- Failure paths should leave outputs unchanged or document the exception.
- Builder self-overlap is a supported append case and must preserve the original source
  bytes.
- Builder growth/copy counters must not change append, reserve, reset, or failure
  semantics.
- Diagnostics should use stable codes and avoid leaking secrets.

## Tests

Core tests live under `tests/unit/core/` and related integration fixtures. They should
verify failure paths, rollback behavior, ownership, and deterministic diagnostics rather
than implementation accidents.
