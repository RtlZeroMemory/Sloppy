# Memory Model

## Purpose

This document defines Sloppy's memory model before Phase 1 core primitives are implemented.
It exists to prevent accidental C string dependence, unclear ownership, unsafe async
lifetimes, and raw pointer exposure to JavaScript.

## Scope

This document covers:

- `SlStr`;
- `SlBytes`;
- `SlBuf`;
- `SlStringBuilder`;
- arena types;
- scope cleanup registration storage;
- allocator rules;
- ownership rules;
- resource table/generation counter model;
- async lifetime rules;
- engine heap separation;
- request-scope lifecycle;
- tests and acceptance criteria.

## Non-Goals

This document does not implement allocators, arenas, resource tables, or JS bindings.

## Current Phase

The core foundation now implements `SlStatus`, `SlSourceLoc`, borrowed `SlStr`, borrowed
`SlBytes`, checked `size_t` arithmetic, assertion macros, a caller-backed `SlArena`, and a
fixed-capacity native cleanup `SlScope`.
`SlBuf`, string builders, allocator modules, and resource table primitives are not
implemented yet.

## Future Phase

Phase 1 starts with core C primitives. Memory APIs should be small, heavily tested, and
independent of V8, HTTP, database providers, or app modules.

## Core Types

Design targets:

```c
typedef struct SlStr {
    const char* data;
    size_t length;
} SlStr;

typedef struct SlBytes {
    const unsigned char* data;
    size_t length;
} SlBytes;

typedef struct SlBuf {
    unsigned char* data;
    size_t length;
    size_t capacity;
} SlBuf;
```

`SlStr` is a borrowed string view. It is not necessarily null-terminated and can contain
embedded NUL bytes as data. `SlBytes` is a borrowed byte view. Neither view allocates,
copies, frees, or transfers ownership. `SlBuf` is future work and will be a mutable owned or
externally-backed buffer depending on API contract.

## SlStringBuilder

`SlStringBuilder` is a future arena-backed builder for diagnostics, paths, and generated
messages. It should:

- append `SlStr`;
- append bytes only through explicit encoding-aware APIs;
- check all size arithmetic;
- expose a final `SlStr` view with documented lifetime;
- never rely on implicit `strlen` except boundary adapters.

## Arena Types

Planned arenas:

- permanent arena: runtime lifetime metadata;
- startup/build arena: app graph construction and validation;
- request arena: per-request transient state;
- scratch arena: bounded temporary work.

Arenas are deliberate, not universal. Independently closed resources must not be stored only
in arenas.

`SlScope` may use caller-provided or arena-provided storage to record cleanup callbacks.
The scope owns the cleanup registrations, not the callback payloads. Arena-backed scope
storage is invalidated by the same arena reset/reset-to-mark rules as other arena
allocations.

Implemented TASK 03.A arena behavior:

- `SlArena` is initialized over caller-provided memory and never owns or frees that memory;
- zero-capacity arenas are valid but cannot allocate;
- allocations require nonzero size and nonzero power-of-two alignment;
- zero-size allocations fail with `SL_STATUS_INVALID_ARGUMENT`;
- allocation checks alignment padding, size addition, and final offsets with checked
  arithmetic;
- marks capture the current offset and can reset allocations above that mark;
- full reset returns used bytes to zero and preserves high-water statistics;
- marks carry a generation counter and marks captured before a full reset are rejected in
  all builds;
- assert-enabled builds poison allocated and reset memory ranges with simple byte patterns;
- arenas are not thread-safe unless externally synchronized;
- arena-backed data must not cross async boundaries unless the arena outlives the operation.

## Ownership Rules

Every public API must document ownership:

- borrowed input;
- copied input;
- caller-owned output;
- arena-owned output;
- resource-table-owned handle;
- invalidation rule.

No implicit ownership transfer. Naming and comments must agree.

## Allocator Rules

- no raw `malloc`/`free` outside allocator modules;
- allocation sizes must use checked math;
- APIs should prefer count plus element size where overflow is possible;
- out-of-memory returns `SlStatus` and emits diagnostics where context exists;
- debug builds should tag allocations by module where practical.

Fixed caller-provided storage exhaustion is reported as `SL_STATUS_CAPACITY_EXCEEDED`
rather than pretending an OS or heap allocation failed.

## Resource Table Model

Resources that can close independently use resource tables.

`SlResourceId` should eventually encode:

- resource kind/table;
- slot index;
- generation counter.

Generation counters prevent stale IDs from referring to reused slots. JS-visible native
resources must use IDs, never raw C pointers.

## Async Lifetime Rules

Arena-backed memory must not cross async boundaries unless the arena outlives the operation.
Data crossing async boundaries must be:

- copied;
- reference-counted;
- explicitly owned;
- stored as a resource table entry.

Request-scope memory remains alive until handler promise settlement, response completion, or
cancellation cleanup. The request arena survives pending promises but must not be used by
native worker-pool work after request cleanup begins.

Data sent to a worker pool must be copied, reference-counted, or otherwise owned safely by
that operation. Worker-pool data must not point into shorter-lived scratch arenas. The
resource table tracks async resources whose lifetime can outlive a single stack frame.

The canonical concurrency and async lifetime rules live in `docs/concurrency.md`.

## Engine Heap Separation

The V8 heap is separate from the C runtime heap. The V8 bridge owns V8 handles and persistent
references. Core runtime modules must not assume JS object layout, V8 allocation behavior,
or GC timing.

## Request-Scope Lifecycle

Target request lifecycle:

1. create request arena;
2. allocate transient parsed/request data;
3. lazily materialize JS context;
4. invoke handler;
5. wait for promise settlement if needed;
6. close scoped resources;
7. detect leaked statements/connections/handles in debug builds;
8. release request arena.

## Forbidden Patterns

- naked internal `char*` string APIs;
- `strlen` in core logic outside boundary adapters;
- unchecked size addition/multiplication;
- VLA allocation;
- returning pointers into scratch arenas;
- JS raw C pointers;
- arena-only ownership for resources that can close independently;
- hidden global allocators.

## File Layout

Likely Phase 1 files:

```text
include/sloppy/status.h
include/sloppy/string.h
include/sloppy/bytes.h
include/sloppy/buf.h
include/sloppy/allocator.h
include/sloppy/arena.h
src/core/status.c
src/core/string.c
src/core/buf.c
src/core/allocator.c
src/core/arena.c
tests/unit/
```

Names may change, but tests must land with primitives.

## Error And Diagnostic Behavior

Memory primitive APIs return `SlStatus` for failure. Diagnostics are emitted only when
context is useful. Low-level allocation helpers should not allocate diagnostics recursively.

Typical status codes:

- invalid argument;
- out of memory;
- overflow;
- stale resource;
- resource already closed.
- invalid state;
- capacity exceeded.

## Testing Requirements

Each primitive needs tests:

- `SlStr`: empty, non-null-terminated, equality, prefix/suffix, invalid args;
- `SlBytes`: empty, binary data with zero bytes, bounds;
- `SlBuf`: append, reserve, overflow, ownership, cleanup;
- `SlStringBuilder`: append, arena lifetime, overflow;
- arenas: allocate, reset, nested scope, high-water tracking;
- scope cleanup: registration, LIFO close, idempotent close, reset behavior, capacity
  exhaustion;
- checked math: boundary values;
- resource table: generation mismatch, double close, stale ID, leak tracking.

## Quality Gates

- C unit tests added with each primitive;
- warnings-as-errors;
- clang-tidy clean;
- sanitizer clean where supported;
- no raw allocator calls outside allocator module;
- no OS API calls outside `src/platform/*`.

## Phase 1 Implementation Tasks

- Add checked math helpers. Done for `size_t` add/multiply in TASK 02.A.
- Add `SlStr` and tests. Done for borrowed views in TASK 02.A.
- Add `SlBytes` and tests. Done for borrowed views in TASK 02.A.
- Add arena skeleton. Done as a caller-backed `SlArena` in TASK 03.A.
- Add `SlBuf` and tests.
- Add allocator interface with a default bootstrap allocator.
- Add debug allocation tags.
- Add resource ID layout proposal before resource table implementation.

## Acceptance Criteria

Phase 1 memory foundation is accepted when:

- primitives compile as C17;
- every public header documents ownership;
- unit tests cover success, failure, and boundary cases;
- no feature code depends on raw C strings internally;
- no JS/V8/database/HTTP behavior is introduced;
- sanitizer-ready code paths avoid undefined behavior.

## Open Questions

- Exact allocator vtable shape.
- Whether `SlStr` tracks encoding or only bytes.
- Whether resource IDs are 64-bit or structured opaque values.
- Exact debug leak report format.
