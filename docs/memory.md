# Memory Model

## Purpose

This document defines Sloppy's memory model before Phase 1 core primitives are implemented.
It exists to prevent accidental C string dependence, unclear ownership, unsafe async
lifetimes, and raw pointer exposure to JavaScript.

## Scope

This document covers:

- `SlStr`;
- `SlBytes`;
- `SlOwnedStr`;
- `SlOwnedBytes`;
- `SlByteBuilder`;
- `SlStringBuilder`;
- `SlInternTable`;
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

This document does not implement a heap allocator framework, JS bindings, or broad subsystem
adoption. V8/SQLite bridge conversion helpers exist through ENGINE-21.D; broad call-site
adoption and refactoring remain deferred to ENGINE-22.

## Current Phase

The core foundation now implements `SlStatus`, `SlSourceLoc`, borrowed `SlStr`, borrowed
`SlBytes`, arena-owned string and byte copy helpers, deterministic string/byte hash helpers,
checked `size_t` arithmetic, assertion macros, a caller-backed `SlArena`, bounded fixed or
arena-backed `SlByteBuilder`/`SlStringBuilder` primitives, a bounded app/static-lifetime
`SlInternTable`, a fixed-capacity native cleanup `SlScope`, a fixed-capacity
`SlResourceTable` with generation-counted `SlResourceId` handles, and a minimal
`SlAppRequestScope` wrapper that closes request cleanups on handler success, failure,
cancellation/deadline-style statuses, and unsupported pre-handler outcomes.
`SlAppLifecycle` now gives the app host an explicit startup/shutdown cleanup scope for
app-lifetime resources. ENGINE-22.A has adopted the shared string/byte copy and builder
primitives in the HTTP parser, request body accumulation, route-pattern copies, route
matching edge tests, and dev response writer while preserving the current complete-buffer
HTTP behavior. ENGINE-22.C has adopted shared arena string copies in Plan parsing, bounded
string builders for artifact/source-map/stdlib path assembly in the current loader, and a
post-parse bounded intern table for stable Plan metadata.

Allocator modules, a standalone heap-owned `SlBuf`, and remaining hot-path adoption remain
deferred. ENGINE-21.D defines the narrow V8/native and SQLite/native string/blob interop
helper policy. ENGINE-22.D adopts those helpers in provider-neutral V8 bridge internals,
HTTP request context materialization, `Results.*` descriptor conversion, and exception
strings. ENGINE-22.E adopts SQLite result/parameter ownership across the native provider
and V8 SQLite bridge. ENGINE-22.F removes a remaining non-SQLite capability diagnostic
hint buffer, keeps OpenAPI path skeleton normalization on the bounded string builder, and
adds a low-capacity denial-hint regression guard.

ENGINE-12.AB adds queued native completion ownership rules. `SlAsyncLoop` stores bounded
completion records in caller-owned storage and takes ownership only after a post succeeds.
On successful post it may retain a request/app scope through `SlAsyncScopeRef`; dispatch or
discard releases that scope exactly once and runs the completion cleanup callback exactly
once. Failed posts, including overflow and disposed-loop posts, do not take ownership, so
the caller remains responsible for cleanup. Queued completions must own or retain any
memory needed after the caller returns; borrowed request-arena views must not be placed in
queued work unless the owning request/app scope is explicitly retained.

ENGINE-23.A/B extends that rule to provider/offload operations. `SlProviderOperation`
descriptors copy provider instance IDs, provider kind, operation name, capability token,
diagnostic context, and operation input bytes into caller-provided arena storage before
submission succeeds. Queued provider work must not retain transient request or scratch
views unless the operation also retains the owning scope. Failed admission leaves caller
ownership intact and does not run the operation cleanup callback. Accepted operation
cleanup is invoked exactly once from the terminal completion/discard path; late completion
after cancellation, timeout, or shutdown must not free or settle twice.

ENGINE-21 and ENGINE-22 are the strategic completion roadmap for this layer:

- ENGINE-21 defines the app/request/temp/static/V8/SQLite/diagnostic lifetime model,
  allocation and failure policy, string/byte/owned-buffer primitives, byte and string
  builders, formatting utilities, bounded app/static string interning and symbol tables,
  V8/native conversion policy, SQLite text/blob ownership, and memory safety/stress tests.
- ENGINE-22 adopts those primitives in hot paths after they exist: HTTP parse/write/body
  adoption is implemented through ENGINE-22.A for current HTTP hot paths; Plan/artifact
  loader and stable parsed-Plan metadata adoption are implemented through ENGINE-22.C for
  the current `sloppy run --artifacts` path; provider-neutral V8 bridge string conversion
  adoption is implemented through ENGINE-22.D; SQLite row/result/parameter conversion is
  implemented through ENGINE-22.E. Remaining diagnostics/source-frame expansion, broader
  CLI output, and conformance/benchmark guards continue as later ENGINE-22 tasks.

The current source audit is `docs/project/memory-string-current-state-audit.md`. The
intended primitive architecture is
`docs/project/memory-string-foundation-architecture.md`. The adoption map is
`docs/project/memory-string-adoption-map.md`.

## Future Work

Future memory work should keep APIs small, heavily tested, and independent of V8, HTTP,
database providers, or app modules unless a scoped adoption task requires that boundary.

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

typedef struct SlOwnedBytes {
    unsigned char* ptr;
    size_t length;
} SlOwnedBytes;
```

`SlStr` is a borrowed string view. It is not necessarily null-terminated and can contain
embedded NUL bytes as data. `SlBytes` is a borrowed byte view. Neither view allocates,
copies, frees, or transfers ownership.

`SlOwnedStr` and `SlOwnedBytes` are ownership markers for memory owned by the documented
producer lifetime. The current public producers are arena-copy helpers, so returned owned
views remain valid until that arena is reset/disposed or its caller-owned backing storage
ends. The type itself does not free memory.

`sl_str_copy_to_arena_nul` is the explicit C-string boundary adapter. The returned length
excludes the appended NUL terminator; ordinary core logic must continue to use explicit
lengths.

## SlByteBuilder And SlStringBuilder

`SlByteBuilder` and `SlStringBuilder` are implemented as bounded output targets for
diagnostics, paths, response bytes, and generated small text. Builders can be initialized
over caller-owned fixed storage or over an arena with an explicit maximum capacity.

They:

- append `SlBytes`, individual bytes, `SlStr`, chars, C-string boundary inputs, and small
  decimal integer formatting helpers;
- check all reserve, growth, and formatting size arithmetic;
- expose borrowed `SlBytes` or `SlStr` views tied to the builder storage lifetime;
- provide `sl_string_builder_view_with_nul` for explicit C-string boundary adapters;
- remain valid after failed reserve/append calls, preserving the already-written prefix;
- never use `sprintf` or hidden heap allocation.

Arena builders allocate replacement buffers from the caller-supplied arena as they grow.
Old arena buffers are abandoned until the arena resets; this is acceptable for scoped
builder lifetimes and keeps ownership simple.

`SlBuf` as a standalone heap/operation-owned buffer type remains deferred until a real
heap allocator or operation-owned response/body buffer contract exists.

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
- `sl_arena_dispose` invalidates the arena object without freeing caller-owned storage.

## String Interning

`SlInternTable` is implemented as a bounded app/static-lifetime table. The caller owns the
table object and the arena; the table allocates metadata and copied string bytes from that
arena and has no process-global pool.

Interned strings are for stable metadata only:

- route names and method tokens;
- module names;
- capability names;
- provider names;
- stable Plan keys;
- diagnostic code/name metadata.

Do not intern request bodies, secrets, connection strings, arbitrary user payloads, or
transient diagnostic text. Pointer identity may accelerate lookup, but byte equality and
stored hash/length remain the correctness rule. Symbols are stable only for metadata within
the table owner's lifetime and become stale when the table is disposed/reinitialized.

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
- ENGINE-05 SQLite JS resources store capability/provider metadata beside the native
  connection so every open/read/write provider call can re-check authority without exposing
  pointers to JavaScript;
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
include/sloppy/builder.h
include/sloppy/intern.h
include/sloppy/allocator.h
include/sloppy/arena.h
include/sloppy/resource.h
src/core/status.c
src/core/string.c
src/core/bytes.c
src/core/builder.c
src/core/intern.c
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
- arena copy helpers: empty, non-NUL-terminated, binary data, capacity failure, unchanged
  output on failure;
- builders: append, reserve, growth, fixed capacity, optional NUL, formatting, overflow,
  failed-append prefix preservation;
- intern table: duplicate insertion, bucket collision/byte equality, capacity failure,
  stale symbols, and unchanged output on failure;
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
- Add standalone `SlBuf` only when a heap/operation-owned buffer contract exists.
- Add allocator interface with a default bootstrap allocator.
- Add debug allocation tags.
- Complete ENGINE-21.D V8/SQLite interop policy. Done: private V8 conversion helpers and
  SQLite text/blob copy helpers exist; broad call-site adoption remains ENGINE-22.
- Complete ENGINE-22 adoption/refactor work for hot paths after ENGINE-21 lands.
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
