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

This document does not implement allocators, string builders, or JS bindings.

## Current Phase

The core foundation now implements `SlStatus`, `SlSourceLoc`, borrowed `SlStr`, borrowed
`SlBytes`, checked `size_t` arithmetic, assertion macros, a caller-backed `SlArena`, a
fixed-capacity native cleanup `SlScope`, a fixed-capacity `SlResourceTable` with
generation-counted `SlResourceId` handles, and a minimal `SlAppRequestScope` wrapper that
closes request cleanups on handler success, failure, cancellation/deadline-style statuses,
and unsupported pre-handler outcomes. `SlAppLifecycle` now gives the app host an explicit
startup/shutdown cleanup scope for app-lifetime resources.
`SlBuf`, string builders, and allocator modules are not implemented yet.

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

Resources that can close independently use `SlResourceTable`.

Implemented MAIN1-07 behavior:

- `SlResourceId` is a plain `{ slot, generation }` value made of two `uint32_t` fields;
- slot zero and generation zero are invalid/null;
- public IDs do not contain native pointers and are safe to wrap in future JS handle
  objects;
- table storage is caller-owned or arena-owned through `sl_resource_table_init_from_arena`;
- table objects are zero-initialized before first init and cannot be reinitialized after
  init/dispose, preventing old IDs from being resurrected by resetting generations;
- table capacity must fit in the `uint32_t` slot range encoded by `SlResourceId`;
- inserts require a non-NULL native pointer and a non-`NONE` kind;
- lookup validates slot, generation, liveness, and expected kind before returning the
  native pointer to trusted C callers;
- kind is table-owned metadata and must never be trusted from JavaScript;
- close invokes the optional cleanup callback exactly once, clears the slot, and advances
  the generation;
- using a closed ID after close fails as a stale handle once the generation advances;
- slot reuse returns the same slot with the next generation;
- table exhaustion returns `SL_STATUS_CAPACITY_EXCEEDED` without mutating caller-owned
  outputs or running cleanup;
- dispose closes remaining live entries in deterministic ascending slot order.

Generation counters prevent stale IDs from referring to reused slots. JS-visible native
resources must use IDs, never raw C pointers.

Resource lookup failures use deterministic status/diagnostic pairs:

- invalid/null ID or missing slot: `SL_STATUS_INVALID_ARGUMENT` or `SL_STATUS_OUT_OF_RANGE`
  with `SL_DIAG_RESOURCE_INVALID_ID`;
- stale generation: `SL_STATUS_STALE_RESOURCE` with `SL_DIAG_RESOURCE_STALE_ID`;
- wrong kind: `SL_STATUS_WRONG_RESOURCE_KIND` with `SL_DIAG_RESOURCE_WRONG_KIND`;
- closed current slot: `SL_STATUS_INVALID_STATE` with `SL_DIAG_RESOURCE_CLOSED`.

Diagnostics may include the operation name plus expected/actual resource kind names. They
must not include native pointer values.

## JS-Native Handle Bridge Policy

Future JS/native bridges must use the resource table rather than inventing ad hoc handles.

- JS may see only opaque resource IDs or handle objects that wrap those IDs.
- JS must never receive raw native pointers, pointer-sized integers, V8 external pointers,
  or provider-owned address strings.
- Every JS-visible native handle maps to a live `SlResourceTable` entry.
- Every bridge call validates the ID slot, generation, liveness, and expected kind before
  touching a provider object.
- Stale and wrong-kind handles fail deterministically with the resource diagnostics above.
- Request/app scope ownership must be explicit before a handle is exposed: app-lifetime
  pools, request-lifetime checked-out resources, and statement/transaction resources cannot
  share an implicit global registry.
- Request-scoped native resources should be represented by `SlResourceId` entries and paired
  with a request-scope cleanup that closes the ID. The app-host lifecycle helpers provide
  caller-owned `SlAppResourceCleanup` payloads that close `SlResourceId` entries through
  `SlScope` on request completion or app shutdown. Provider bridges still need to decide
  which concrete handles are request-scoped versus app-scoped before public APIs claim
  automatic provider lifetime behavior.
- MAIN1-08 SQLite bridge work consumes `SlResourceId`/`SlResourceTable`; it must not
  reinvent handle storage or expose SQLite pointers.
- V8 owns the common resource table on the engine backend. Provider-specific bridge modules
  under `src/engine/v8/intrinsics_<provider>.cc` may insert, look up, and close their own
  resource kinds through that table, while `engine_v8.cc` remains provider-neutral.

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

Implemented request-scope behavior is still intentionally small: a native request scope
begins before the handler boundary and closes after handler success, synchronous failure,
unsupported pre-handler outcomes, cancellation/deadline-style statuses, and the bounded V8
Promise resolve/reject/pending paths. Cleanup callbacks run in `SlScope` LIFO order. The
scope owns cleanup registrations only; cleanup payloads and user data remain caller-owned
and must outlive cleanup execution. `SlAppLifecycle` provides the matching app startup and
shutdown cleanup scope; shutdown is idempotent and uses the same LIFO cleanup contract.
Cleanup callbacks are currently void/no-fail, so rich cleanup-failure diagnostics remain
deferred.

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

Implemented and planned Phase 1 files:

```text
include/sloppy/status.h
include/sloppy/string.h
include/sloppy/bytes.h
include/sloppy/buf.h
include/sloppy/allocator.h
include/sloppy/arena.h
include/sloppy/resource.h
src/core/status.c
src/core/string.c
src/core/buf.c
src/core/allocator.c
src/core/arena.c
src/core/resource.c
tests/unit/
```

`include/sloppy/resource.h` defines `SlResourceId`, `SlResourceKind`, `SlResourceEntry`,
and `SlResourceTable`; `src/core/resource.c` implements the fixed-capacity resource table
used by future JS-native handles. Names may change only with matching tests and docs.

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
- resource table: invalid IDs, generation mismatch, wrong kind, double close, cleanup,
  exhaustion, and dispose ordering.

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
- Add resource ID layout and fixed-capacity resource table. Done in MAIN1-07 with
  slot/generation IDs, kind validation, cleanup callbacks, and bridge policy docs.
- Add app/request-scope resource ownership and leak reporting. Follow-up app-host
  lifecycle work.

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
