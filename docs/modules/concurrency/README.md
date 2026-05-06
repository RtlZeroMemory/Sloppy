# Concurrency Module

## Status

Partially implemented for TASK 09.A, TASK 09.B, TASK 09.C, the ENGINE-03 V8 async
runtime slice, and CORE-WORKER-01 bootstrap worker resources.

TASK 10.B adds libuv as a vcpkg/CMake dependency and a minimal init/close smoke under the
HTTP parser tests. It does not change the implemented `SlLoop`, `SlAsync`, or
`SlWorkerPool` semantics.

## Purpose

Define Sloppy's event loop, completion queue, owner-thread, promise settlement, and worker
pool model.

## Scope

Implemented now:

- `SlLoop` as a caller-backed, fixed-capacity native completion queue;
- synchronous `sl_loop_run_once` and `sl_loop_drain` dispatch;
- stop/reset behavior for deterministic single-threaded tests.
- `SlAsync` as a caller-owned native settlement skeleton over `SlLoop`;
- manual fulfilled, rejected, and cancelled settlement states for deterministic tests.
- `SlWorkerPool` as an inline/fake worker-pool design skeleton over `SlLoop`;
- worker work item callbacks, completion callbacks, and result destroy callbacks;
- deterministic completion-posting tests without real threads.

Future scope still includes real event-loop backends, real worker threads, native async
provider completion queues, richer deadline hooks, HTTP disconnect propagation, shutdown
drain/cancel policy, and thread-safe posting. ENGINE-03 adds V8 owner-thread microtask
draining for returned handler Promises plus a native cancellation-token snapshot shape, but
it does not add timers, fetch, Node APIs, or queued native completions.

CORE-WORKER-01 adds `sloppy/workers` as the public worker resource API. In the bootstrap
stdlib, `BackgroundService`, bounded `WorkQueue`, and `WorkerPool` admission semantics run
today; V8 installs `__sloppy.workers` feature metadata. True native CPU offload and separate
V8 worker isolates are still bridge-gated and must not be counted as pass evidence without
V8-gated tests.

## Non-goals

No libuv backend, OS event loop, timers, sockets, HTTP server behavior, real worker
threads, atomics, locks, native provider async queues, blocking DB/filesystem work, or
cross-thread posting behavior in TASK 09.C, TASK 10.B, or ENGINE-03.

## Public/Internal API

Implemented public header:

- `include/sloppy/loop.h`
- `include/sloppy/async.h`
- `include/sloppy/cancellation.h`
- `include/sloppy/worker_pool.h`
- `stdlib/sloppy/workers.js`

Implemented API:

- `sl_loop_init`;
- `sl_loop_reset`;
- `sl_loop_post`;
- `sl_loop_run_once`;
- `sl_loop_drain`;
- `sl_loop_stop`;
- `sl_loop_pending_count`;
- `sl_loop_capacity`;
- `sl_loop_is_stopped`.
- `sl_async_init`;
- `sl_async_state`;
- `sl_async_is_pending`;
- `sl_async_is_settled`;
- `sl_async_fulfill`;
- `sl_async_reject`;
- `sl_async_cancel`.
- `sl_worker_pool_init_inline`;
- `sl_worker_pool_reset_inline`;
- `sl_worker_pool_submit`.
- `BackgroundService.create`;
- `WorkQueue.create`;
- `WorkerPool.create`;
- `Worker.start`.

Real native worker-thread execution for `WorkerPool` and separate JavaScript worker isolate
execution remain future work beyond the bootstrap API and metadata path.

## Ownership/Lifetime Rules

`SlLoop` owns completion registrations only. It does not own callback payloads or user data;
those pointers are borrowed by the loop and may be NULL.

Completion storage is caller-owned. `sl_loop_init` accepts a caller-provided
`SlCompletion` array and fixed capacity. Zero-capacity loops are valid but cannot accept
posts.

Future request scopes live until promise settlement, response completion, or cancellation
cleanup.

`SlAsync` is caller-owned and does not allocate. Storage passed to `sl_async_init` must be
zero-initialized before first use or already initialized by `sl_async_init`. The object owns
only the settlement record fields. Payload, result user data, continuation user data, and
diagnostics are borrowed. Borrowed diagnostics must remain valid until loop dispatch; real
V8/request scope integration will define a stronger ownership contract later.

`SlWorkerPool` is caller-owned and does not allocate. Storage must be zero-initialized
before first use, or already initialized by `sl_worker_pool_init_inline`. Inline completion
records live inside the pool until `SlLoop` dispatches them. `SlWorkItem.payload`,
`SlWorkItem.user`, and completion user pointers are borrowed and may be NULL. A non-NULL
result produced by `SlWorkFn` is owned by the completion callback once that callback runs.
If posting the completion fails before dispatch, the pool calls
`SlWorkItem.destroy_result` for that result when a destroy callback is available. If a
caller discards queued worker completions with `sl_loop_reset`, it must call
`sl_worker_pool_reset_inline` afterward so pending results are destroyed and inline records
are released.

## Invariants

Implemented loop invariants:

- completions dispatch in FIFO order;
- `sl_loop_post` rejects NULL callbacks;
- posting fails with `SL_STATUS_CAPACITY_EXCEEDED` when the queue is full;
- posting after `sl_loop_stop` fails with `SL_STATUS_INVALID_STATE`;
- `sl_loop_run_once` invokes at most one completion;
- `sl_loop_drain` runs until the queue is empty, the loop is stopped, or a callback fails;
- a completion is consumed before its callback runs;
- failed callbacks are not retried automatically;
- callbacks run synchronously on the caller thread;
- callbacks may post more completions when capacity is available;
- callbacks may call `sl_loop_stop`;
- nested drains on the same loop fail with `SL_STATUS_INVALID_STATE`;
- `sl_loop_reset` clears pending completions and the stopped flag without invoking
  callbacks.
- async objects start in `SL_ASYNC_STATE_PENDING`;
- async continuations are required at initialization;
- fulfillment stores an OK status and borrowed payload/user pointers;
- rejection and cancellation require a non-OK status and may carry a borrowed diagnostic;
- settlement posts exactly one `SL_COMPLETION_KIND_ASYNC` completion;
- continuations run from `sl_loop_run_once` or `sl_loop_drain`, never inline during
  settlement;
- reinitialization while a settled async completion is still queued fails with
  `SL_STATUS_INVALID_STATE`;
- reinitialization after the queued completion drains is allowed;
- double settlement fails with `SL_STATUS_INVALID_STATE`;
- failed loop posting leaves the async object pending and without a stored result;
- continuation failure propagates through the loop dispatch call.

Concurrency invariants:

- this skeleton is single-threaded and does not enforce owner-thread identity yet;
- cross-thread posting is not supported yet;
- cross-thread async settlement is not supported yet;
- worker-pool submission is inline only and not thread-safe;
- worker completions are posted to `SlLoop` and are not invoked directly by submit;
- worker result ownership transfers only at completion dispatch;
- worker-pool reinitialization is rejected while inline completions are pending;
- `sl_worker_pool_reset_inline` is the supported cleanup after `sl_loop_reset` discards
  queued worker completions;
- native worker threads must never call JS directly;
- V8 isolates must only be entered by their owning JS event-loop thread.

## Diagnostics

TASK 09.A loop APIs, TASK 09.B async APIs, and TASK 09.C worker-pool APIs return `SlStatus`
only and do not emit diagnostics. `SlAsync` may borrow an existing diagnostic for
rejected/cancelled settlement, but it does not build, copy, render, or own diagnostics.
Worker-pool submission reports invalid arguments, unsupported mode, fixed-record capacity,
and loop post failures through `SlStatus`. V8 Promise rejection, route/request-aware
cancellation, overload, and wrong-thread diagnostics remain required follow-up work for the
real async foundation.

## Tests

CTest registers `tests/unit/core/test_loop.c`, covering:

- valid and invalid initialization, including zero-capacity behavior;
- posting one or multiple completions;
- payload/user forwarding, including NULL payloads;
- NULL callback rejection;
- full-queue failure and failed-post atomicity;
- count/capacity/stopped helpers;
- `run_once` dispatching exactly one completion;
- FIFO ordering;
- `drain` running all available completions and reporting callback count;
- callbacks posting additional completions;
- callbacks stopping the loop;
- post-after-stop behavior;
- reset clearing pending work and stopped state;
- empty drain behavior;
- callback failure propagation;
- nested drain rejection.

CTest registers `tests/unit/core/test_async.c`, covering:

- valid initialization, NULL async rejection, and NULL continuation rejection;
- initial pending state helpers;
- fulfilled settlement posting through `SlLoop`;
- loop drain invoking continuations with fulfilled state, payload, and user pointers;
- rejected settlement with non-OK status and borrowed diagnostic pointer;
- cancelled settlement with non-OK status and borrowed diagnostic pointer;
- invalid OK rejection/cancellation status rejection;
- double settlement and cross-state settlement failure;
- loop post failure leaving async pending;
- reinit-before-drain rejection and preservation of the original queued completion result;
- NULL loop and NULL async settlement rejection;
- continuation failure propagation through loop drain;
- deterministic completion order across multiple async objects.

CTest registers `tests/unit/core/test_worker_pool.c`, covering:

- inline pool initialization and invalid initialization;
- submit validation for NULL pool, loop, item, work callback, completion callback, invalid
  kind, and unsupported/uninitialized mode;
- inline work execution with completion posted to `SlLoop` rather than called inline;
- completion result/status/user forwarding;
- FIFO completion order for multiple submitted work items;
- work failure posting a failure status;
- loop queue-full post failure returning failure and destroying produced results exactly
  once;
- loop reset followed by worker-pool reset destroying pending results and freeing records;
- reinitialization before loop drain failing without corrupting the queued completion;
- completion callback failure propagation through loop drain without result double destroy;
- NULL payload and completion user behavior.

TASK 10.B also registers `core.http.parser`, which includes a libuv loop init/close smoke
only to verify the dependency links.

ENGINE-03 V8-gated tests cover Promise settlement through the owner-thread microtask drain,
rejection diagnostics, pending-Promise deadline diagnostics, wrong-thread rejection,
cancellation snapshots, and request-scope cleanup across resolve/reject/pre-cancel/pending
failure paths. Thread-safe posting, real libuv/backend behavior, native provider async
queues, HTTP disconnect/shutdown cancellation, and no-V8-entry worker tests remain future
work.

EPIC-15 adds only JavaScript fake data-provider transaction tests. Those tests verify the
public callback contract (commit on resolve, rollback on throw/reject, no nested
transactions, no use after close) without using `SlLoop`, worker pools, native resources,
database connections, or SQL execution.

EPIC-16 adds native SQLite transaction tests, but they are synchronous C provider tests and
do not use `SlLoop`, `SlWorkerPool`, V8 promises, cancellation, deadlines, or JavaScript
request scopes.

## Source Docs

- `docs/concurrency.md`;
- `docs/execution-model.md`;
- `docs/memory.md`;
- `docs/testing-strategy.md`;
- ADR 0014.

## Open Questions

- Exact event loop backend integration.
