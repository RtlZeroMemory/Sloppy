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
- deterministic string/byte equality, hashing, and scalar byte search helpers;
- checked `size_t` arithmetic, including array-allocation and three-term-addition helpers;
- assertion macros;
- caller-backed `SlArena`;
- bounded fixed or arena-backed byte and string builders;
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

## Builders

Builders are bounded output primitives. They exist to replace repeated ad hoc fixed-buffer
formatting in diagnostics, Plan/artifact paths, HTTP response serialization, CLI output,
and other hot or failure-sensitive paths.

Overflow is a normal error path. Builder failures must not silently truncate semantic
output unless the contract explicitly defines truncation.

Builder appends are allowed to read from the builder's current storage. Overlapping
self-appends must behave as if the source bytes were captured before the append, so callers
do not need a scratch copy for length-preserving byte/string duplication.

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
tests, sanitizer lane expansion, provider executor/offload adoption for all provider paths,
and any future heap-owned buffer abstraction that earns its place through a real ownership
need.

Historical audits and adoption maps live under `docs/project/archive/`.
