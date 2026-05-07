# Memory Model

## Purpose

Sloppy's C runtime treats ownership as part of the API contract. This document defines the
current memory model for borrowed views, arena-owned data, bounded builders, resource
handles, request scopes, async completions, and native/JavaScript boundaries.

## Core Rules

- `SlStr` and `SlBytes` are borrowed views. They do not imply NUL termination, ownership, or
  stable lifetime beyond the documented owner.
- C-string boundaries are explicit. A component that must pass a Sloppy string to an OS,
  libuv, env, config, or other NUL-terminated API must validate that the borrowed `SlStr`
  contains no embedded NUL before copying it to a terminated arena string.
- Arena-owned strings and bytes remain valid until the arena is reset or its backing storage
  ends.
- Callers must use checked arithmetic for sizes and offsets that can overflow.
- Public/runtime contracts must state whether returned views are borrowed, arena-owned,
  heap-owned, or operation-owned.
- JS never receives raw native pointers.
- Cross-thread or delayed completion data must be owned, copied, or retained through an
  explicit scope before the caller returns.
- Cleanup callbacks run at most once.

## Implemented Primitives

The current runtime includes:

- `SlStatus` and `SlSourceLoc`;
- borrowed `SlStr` and `SlBytes`;
- arena-owned string and byte copy helpers, including a C-string boundary helper that
  rejects embedded NUL before appending a terminator;
- deterministic string/byte equality, hashing, and canonical byte search helpers with a
  scalar reference path;
- checked `size_t` arithmetic, including array-allocation and three-term-addition helpers;
- assertion macros;
- caller-backed `SlArena` with read-only stats snapshots;
- bounded fixed, small-inline, or arena-backed byte and string builders with deterministic
  internal growth/copy counters;
- app/static-lifetime intern tables for stable metadata;
- fixed-capacity cleanup scopes;
- generation-counted resource IDs and resource tables;
- app and request lifecycle cleanup scopes;
- async completion records with explicit scope retention and discard paths.

## Arenas

Arenas are caller-backed. The owner provides storage and decides the lifetime. Functions
that write into an arena must leave outputs unchanged on failure unless their contract says
otherwise, and should use marks/rollback when parsing or validation can fail after partial
allocation.

Aggregate result APIs that publish arena-backed rows, columns, or values must be
deterministic on failure: either keep the documented unchanged-output contract or clear the
aggregate before returning. If an arena mark is rolled back, no output field may continue to
point into the reset range.

Request-scoped arenas are for one request. App/static arenas are for validated metadata and
startup-owned resources. Scratch arenas must not leak views to longer-lived owners.

Public rendering boundaries must treat non-empty `SlStr`/`SlBytes` views with `NULL`
storage as malformed input and fail or fall back deterministically instead of dereferencing
the view.

`sl_arena_stats` reports capacity, used bytes, remaining bytes, high-water bytes, and the
current generation without mutating the arena. It is internal evidence for tests,
benchmarks, and future optimization decisions; it is not an allocator abstraction.

## Builders

Builders are bounded output primitives. They exist to replace repeated ad hoc fixed-buffer
formatting in diagnostics, Plan/artifact paths, HTTP response serialization, CLI output,
and other hot or failure-sensitive paths.

Overflow is a normal error path. Builder failures must not silently truncate semantic
output unless the contract explicitly defines truncation.

Builder appends are allowed to read from the builder's current storage. Overlapping
self-appends must behave as if the source bytes were captured before the append, so callers
do not need a scratch copy for length-preserving byte/string duplication.

Small builders provide explicit small-string/small-byte optimization for local construction.
Their storage is inline inside the builder object, never grows, and has builder lifetime.
They must not be used for outputs that outlive the builder unless the result is copied or
materialized into the documented owner first. Arena-backed builders remain the right choice
for APIs that return arena-owned views.

Builder stats report length, capacity, max capacity, grow count, copied bytes, appended
bytes, failed reserve count, and storage kind. Counters are deterministic internal
measurement evidence and do not change append, reserve, reset, or failure semantics.

## SIMD Backends

Scalar byte/string primitives are canonical. Optional SIMD backends may accelerate a
canonical primitive only when the scalar contract, tests, and fallback behavior already
exist. A SIMD backend must preserve pointer-plus-length semantics, embedded-zero behavior,
failure-output rules, and deterministic first-match results.

`SLOPPY_ENABLE_SIMD=AUTO` is the default. Supported architectures enable available
compile-time SIMD backends automatically; unsupported architectures keep the same public
APIs and use the scalar reference path. `SLOPPY_ENABLE_SIMD=OFF` forces scalar fallback,
and `SLOPPY_ENABLE_SIMD=ON` requires a supported backend or fails configuration.
`SLOPPY_SIMD_LEVEL=AUTO` selects the safe baseline backend; `SLOPPY_SIMD_LEVEL=AVX2`
builds the advanced backend for AVX2-targeted artifacts.

The initial backends cover byte find, byte find-any, no-NUL scans, and ASCII
case-insensitive string comparison. SSE2 is the default x86/x64 baseline; AVX2 is available
through the explicit `windows-avx2` preset and equivalent CMake configuration. Both are
covered by the same unit/property/fuzz/benchmark smoke evidence as the scalar path and do
not make a public performance claim.

## Interned Metadata

Intern tables are for stable app/static metadata such as Plan symbols, route names,
provider tokens, capability metadata, and other validated identifiers. They are not for
secrets, request bodies, transient diagnostics, or user data that should be short-lived.

## Resource Handles

Native resources exposed across runtime boundaries use generation-counted IDs rather than
raw pointers. A resource lookup must validate the table, ID, generation, expected type, and
liveness before use. Stale handles fail deterministically.

## Async Ownership

Queued completions must own or retain all data needed after the caller returns. A successful
post transfers the documented operation ownership to the async loop. Failed admission does
not transfer ownership; the caller remains responsible for cleanup.

Late completions after cancellation, timeout, shutdown, or discard are cleanup-only work.
They must not re-enter user code or report success after the owner scope has ended.

## Engine And Provider Boundaries

V8 strings and values are converted inside the bridge. Native strings/results copied out of
V8 become Sloppy-owned C views before returning through the ABI. SQLite text/blob
parameters and results use explicit copy helpers so synchronous calls and future offload
paths share the same ownership rule.

Provider executor submissions copy textual metadata and operation input bytes before work
can outlive the caller stack. Borrowed cancellation/deadline/scope references must remain
valid through the operation or be explicitly retained.

## Deferred Work

Deferred memory work includes broader hot-path adoption, more allocation-aware regression
tests, provider executor/offload adoption for all provider paths, and any future heap-owned
buffer abstraction that earns its place through a real ownership need.

Historical audits and adoption maps live under `docs/project/archive/`.
