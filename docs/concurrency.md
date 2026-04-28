# Concurrency and Async Model

## Purpose

This document defines how Sloppy handles many concurrent requests, async operations, native
worker work, JavaScript promise continuations, and future multicore scaling.

## Scope

This covers the JS event loop model, V8 isolate ownership, native async I/O, native worker
pools, request scopes across promises, database provider async strategy, cancellation and
deadlines, backpressure, CPU-bound work, future multiple JS workers/isolates, and conceptual
differences from ASP.NET Core, Node, Bun, and Deno.

## Non-Goals

- No thread-per-request model.
- No arbitrary thread-pool continuation into a shared V8 isolate.
- No parallel execution of JS callbacks inside one isolate.
- No worker implementation in v0.1.
- No custom event loop implementation in this spec pass.
- No libuv integration in this task.
- No CPU-parallel JS execution in a single isolate.

## Current Implementation

TASK 09.A implements the first `SlLoop` skeleton as a caller-backed, fixed-capacity native
completion queue. It is deterministic and single-threaded: callbacks run synchronously on
the caller thread through `sl_loop_run_once` or `sl_loop_drain`. It creates no threads,
uses no OS APIs, has no libuv backend, and does not settle promises or drive HTTP.

TASK 09.B implements `SlAsync`, the first native promise settlement model skeleton over
`SlLoop`. It represents pending, fulfilled, rejected, and cancelled native work; settlement
posts one completion to the loop and invokes a caller-provided continuation only when the
loop drains. It is manual/fake native settlement only: there is still no JS Promise API
integration, V8 microtask handling, request scope retention, HTTP lifecycle, worker pool,
cross-thread posting, cancellation token, deadline, or backpressure behavior.

TASK 09.C implements `SlWorkerPool`, the first worker-pool design skeleton. Only the inline
backend exists. It runs `SlWorkFn` synchronously on the caller thread, stores a small
caller-owned completion record in `SlWorkerPool`, and posts completion to `SlLoop`.
Completion callbacks are never invoked directly by submit; they run when
`sl_loop_run_once` or `sl_loop_drain` dispatches the posted completion. There are still no
real worker threads, OS APIs, libuv, cross-thread posting, blocking DB/filesystem workers,
HTTP integration, cancellation tokens, deadlines, or backpressure behavior.

TASK 10.B adds libuv as a vcpkg/CMake dependency for the HTTP foundation and proves linkage
with a stack-local loop init/close smoke. It does not add a libuv backend for `SlLoop`,
thread-safe posting, socket I/O, timers, owner-thread checks, or request lifecycle
integration.

The remaining event loop backend, real worker pool, request lifecycle, V8 promise
integration, and async backend behavior is still future work.

## Core Decision

```text
Sloppy uses one JavaScript execution thread per JS worker/isolate.
The owning JS event-loop thread is the only thread allowed to enter that isolate.
Native I/O may complete elsewhere, but completion callbacks are posted back to the owning JS event loop.
```

Sloppy is not thread-per-request. Many requests can be in flight because operations yield to
native async work, not because one V8 isolate runs many JS continuations in parallel.

## Comparison With ASP.NET Core

ASP.NET Core uses .NET ThreadPool workers. `await` releases a worker while I/O is pending.
The continuation may resume on another ThreadPool thread, and many requests may execute
continuations concurrently across different ThreadPool threads.

Sloppy cannot and should not copy that model for JavaScript inside one V8 isolate. A V8
isolate has a strict owner-thread rule in Sloppy, so random pool threads must not enter it.

```text
ASP.NET Core:
request starts on ThreadPool thread A
await I/O
thread A returns to pool
I/O completes
continuation resumes on ThreadPool thread B
```

## Comparison With Node/Bun/Deno

Node, Bun, and Deno are closer to Sloppy's JS model. One JS worker/event-loop thread
executes JS callbacks sequentially. Many requests are in flight because I/O is async.
CPU-heavy JS blocks that worker. Concurrency is I/O concurrency, not parallel JS execution
inside one worker. CPU scaling uses workers, processes, or isolates.

```text
JS worker:
request A enters handler
handler starts async DB query and yields
request B enters handler
DB for A completes
A continuation is queued
A continuation runs on JS thread
```

## Sloppy v0/v1 Threading Model

Main/runtime thread:

- owns native host startup and initial JS worker.

JS event-loop thread:

- owns one V8 isolate/context;
- calls JS route handlers;
- drains microtasks;
- receives native completion events.

Native event loop/backend:

- handles socket readiness/completions;
- drives timers;
- posts work completion.
- future work; TASK 09.A only provides a synchronous test-loop completion queue.

Native worker pool:

- runs blocking or CPU-heavy native operations;
- never enters V8 directly;
- posts completion back to the JS event loop.
- future work; TASK 09.C only proves this completion-posting contract through an inline
  fake backend.

Request scope:

- lives until the handler promise settles or the request is cancelled.

## V8 Isolation Rules

One isolate is entered only by its owning JS thread. V8 types do not cross into core
runtime. Native worker pool threads must not call JS handlers. Cross-thread communication
uses runtime queues/completion messages. Future workers use separate isolates. Any
exception must be owned and reported on the JS thread/engine bridge.
TASK 07.D keeps V8 exception capture inside the bridge and copies diagnostic text into
Sloppy-owned arena storage before returning to C; it does not introduce cross-thread engine
entry or async promise rejection policy.

The current `SlEngine` ABI is not thread-safe. TASK 07.B documents the future owner-thread
rule at the C boundary but does not create threads, enforce owner identity, initialize a V8
isolate, or provide cross-thread queues. Those checks land with later V8 bridge and event
loop tasks.

TASK 07.C creates a V8 isolate/context for the opt-in smoke bridge and enters it only on the
calling thread. It still does not create workers, event-loop queues, or owner-thread
enforcement. Callers must treat the engine as single-thread-owned until the later bridge and
event-loop tasks add explicit checks.

TASK 09.A adds the first native completion queue shape that later backends can post into,
but the current skeleton is still single-threaded. Cross-thread posting, owner-thread
identity checks, wakeups, and worker-thread-safe APIs are not implemented yet. Completion
callbacks in the skeleton run on the caller thread, so they must not be used to bypass the
V8 owner-thread rule.

TASK 09.C adds an inline worker-pool skeleton on top of this queue. The work callback also
runs on the caller thread today, but its completion is still queued to `SlLoop`. Future real
worker threads must keep the same owner-loop completion rule and must never enter a V8
isolate from worker code.

## Current SlLoop Skeleton Semantics

`SlLoop` uses caller-provided `SlCompletion` storage. It never allocates memory, calls OS
APIs, starts threads, or depends on libuv.

The queue semantics are:

- FIFO dispatch;
- fixed capacity;
- posting to a full queue returns `SL_STATUS_CAPACITY_EXCEEDED`;
- posting after `sl_loop_stop` returns `SL_STATUS_INVALID_STATE`;
- `sl_loop_run_once` runs at most one completion;
- `sl_loop_drain` runs until the queue is empty, the loop is stopped, or a callback fails;
- `sl_loop_stop` prevents further drain after the current callback returns;
- `sl_loop_reset` clears pending completions and the stopped flag;
- callbacks may post more completions while capacity is available;
- callbacks may call `sl_loop_stop`;
- nested drains on the same loop are rejected with `SL_STATUS_INVALID_STATE`;
- callback failure propagates through `run_once`/`drain`;
- consumed completions are not retried after callback failure.

This is intentionally not a real OS event loop. libuv integration, IOCP/epoll/kqueue,
timers, sockets, HTTP, thread-safe posting, worker pools, V8 Promise integration,
microtask draining, request lifecycle, cancellation tokens, deadlines, and backpressure are
deferred.

## Current SlAsync Settlement Skeleton Semantics

`SlAsync` is a caller-owned native settlement record. It never allocates memory and does not
own payload, user, or diagnostic pointers. `SlAsyncResult.diag` is borrowed and must remain
valid until loop dispatch. Future request-scope and V8 work will define stronger diagnostic
ownership for real async handlers.

The settlement semantics are:

- storage passed to `sl_async_init` must be zero-initialized before first use or already
  initialized by `sl_async_init`;
- initial state is `SL_ASYNC_STATE_PENDING`;
- continuation is required at initialization;
- `sl_async_fulfill` stores an OK result plus borrowed payload/user pointers;
- `sl_async_reject` stores a non-OK status plus optional borrowed diagnostic;
- `sl_async_cancel` stores a non-OK status plus optional borrowed diagnostic;
- fulfillment, rejection, and cancellation post exactly one `SL_COMPLETION_KIND_ASYNC`
  completion to `SlLoop`;
- the continuation runs only when `sl_loop_run_once` or `sl_loop_drain` dispatches that
  completion;
- continuation failure propagates through the loop drain/run call;
- only pending async objects can settle;
- double settlement fails with `SL_STATUS_INVALID_STATE`;
- NULL async or loop arguments fail with `SL_STATUS_INVALID_ARGUMENT`;
- rejected/cancelled settlement with `SL_STATUS_OK` fails with
  `SL_STATUS_INVALID_ARGUMENT`;
- if loop posting fails, settlement returns that failure and leaves the async object
  pending with no stored result.
- reinitializing a settled async object before its queued completion drains fails with
  `SL_STATUS_INVALID_STATE`;
- reinitialization is allowed after the queued completion drains because the completion has
  copied the original state and result for dispatch.

`SlAsync` is not thread-safe. Settlement must occur on the owning runtime thread for now.
Cross-thread settlement and thread-safe completion queues are future worker-pool/event-loop
work. Current completion callbacks run on the loop caller thread, so this skeleton must not
be used to bypass the V8 isolate owner-thread rule.

## Current SlWorkerPool Inline Skeleton Semantics

`SlWorkerPool` is a caller-owned worker-pool design skeleton. Today it exposes
`sl_worker_pool_init_inline` to initialize inline storage, `sl_worker_pool_submit` to run
work inline and post completion to `SlLoop`, and `sl_worker_pool_reset_inline` to clean up
pending inline completions after callers deliberately discard them from `SlLoop`. It never
allocates memory, starts threads, calls OS APIs, uses locks/atomics, depends on libuv,
performs blocking DB/filesystem work, or enters V8.

The inline worker-pool semantics are:

- storage passed to `sl_worker_pool_init_inline` is caller-owned and must be
  zero-initialized before first use;
- `sl_worker_pool_submit` requires a pool, completion loop, work item, work callback, and
  completion callback;
- payload, work user, and completion user pointers may be NULL;
- `SL_WORK_KIND_NONE` and unknown work kinds are rejected;
- work callbacks run synchronously on the caller thread before submit returns;
- completion callbacks are posted to `SlLoop` and do not run inline during submit;
- `sl_loop_run_once` or `sl_loop_drain` invokes worker completions in loop FIFO order;
- work success posts an OK status and any produced result;
- work failure posts the failure `SlStatus` and any produced result;
- result ownership transfers to the completion callback when the loop dispatches that
  callback;
- if completion posting fails before dispatch, the worker pool destroys a non-NULL result
  with the submitted `destroy_result` callback when one exists;
- if a caller deliberately discards queued worker completions by calling `sl_loop_reset`,
  it must then call `sl_worker_pool_reset_inline` to destroy pending worker results and
  free inline completion records;
- `sl_worker_pool_init_inline` rejects reinitialization while worker completions are
  pending;
- completion callback failure propagates through the loop drain/run call;
- the skeleton stores only a small fixed number of pending inline completion records and
  returns `SL_STATUS_CAPACITY_EXCEEDED` when those records are exhausted;
- the skeleton is not thread-safe and does not support cross-thread submission or posting.

Future real worker pools must run `SlWorkFn` outside the JS event-loop thread only after a
thread-safe completion-posting path exists. They must post completion back to the owning
`SlLoop` before any JavaScript continuation, promise settlement, request cleanup, or engine
work occurs.

## Request Lifecycle With Async Handler

1. Socket receives bytes.
2. Native HTTP parser produces request.
3. Native router matches route.
4. Request scope is created.
5. Runtime calls JS handler by handler ID.
6. Handler returns value or Promise.
7. If value: convert result and write response.
8. If Promise: keep request scope alive and return to event loop.
9. Native async operation completes.
10. Completion posts back to JS event loop.
11. Promise resolves/rejects.
12. Continuation runs on JS thread.
13. Result converts to native response.
14. Response writes.
15. Scoped resources dispose.
16. Request arena resets.

## Promise and Microtask Handling

After calling into JS, the engine bridge must drain or schedule microtasks according to
runtime policy. Rejected promises become diagnostics. Request scope remains alive while a
promise is pending. Promise settlement triggers response or error handling. Unhandled
rejections should include route and handler context when possible.

TASK 09.B does not implement this V8 behavior. It only defines the native settlement record
and loop-continuation shape that future V8 Promise resolution can map onto or evolve.

## Request Scope Lifetime

The request arena exists until response completion or cancellation cleanup. Scoped services
live until the request finishes. DB transactions/resources tied to the request must
close/rollback/dispose on cancellation or error. Data crossing async boundaries must not
point into shorter-lived arenas. Debug builds should detect leaked request resources.

## Native Async Operations

Class A: naturally async socket/event-loop operations:

- network readiness;
- timers;
- server socket accept;
- maybe nonblocking provider sockets later.

Class B: blocking operations using worker pool:

- SQLite queries if blocking;
- SQL Server ODBC calls if blocking;
- blocking filesystem operations;
- compression/crypto later if blocking or CPU-heavy.

Class C: CPU-heavy JS:

- not run on the worker pool automatically;
- use future workers/tasks APIs.

## Database Provider Async Strategy

The public JS database API is always async and promise-friendly. The provider chooses the
implementation strategy. SQLite likely uses a dedicated DB executor or worker pool first.
PostgreSQL/libpq may use blocking worker-pool mode first or nonblocking socket integration
later. SQL Server/ODBC likely uses a worker-pool strategy first.

Transactions pin their connection/resource until the async callback settles. Completions
post back to the JS event loop. Providers must support cancellation/deadline where possible,
or document when they cannot.

```ts
await db.transaction(async tx => {
  await tx.exec`insert into users (name) values (${"Ada"})`;
});
```

The transaction scope remains alive until the callback settles. Rollback occurs on a thrown
or rejected callback unless the transaction helper has already committed by policy.

## Cancellation and Deadlines

Future model:

- each request has a cancellation token;
- client disconnect triggers cancellation;
- configured timeout/deadline triggers cancellation;
- native operations receive cancellation where supported;
- unsupported cancellation is diagnosed/documented;
- request cleanup still runs;
- cancelled request should not resume the normal response path.

## Backpressure

Future model:

- limit request body size;
- limit pending requests per worker;
- limit worker-pool queue depth;
- limit DB pool checkout;
- streaming responses respect socket backpressure;
- overload returns controlled errors instead of unbounded memory growth.

## Scaling to Many Requests

One JS worker can hold many in-flight I/O-bound requests. JS callbacks execute sequentially
on that worker. Keeping handlers short and async is critical. Native route matching and
native preflight reduce JS work. Request arenas and resource tracking help memory stability.

## Scaling to Many CPU Cores

Future model:

- multiple JS workers/isolates;
- `--workers=N` or config later;
- `workers: "auto"` later;
- each worker has a separate V8 isolate/event loop;
- `app.plan` can be shared/read-only;
- request distribution strategy is future;
- worker health/graceful restart is future.

## CPU-Bound Work

CPU-heavy JS blocks a worker. Future APIs may include JS workers, `ctx.tasks.run(...)`, or a
native task provider. Sloppy will not automatically parallelize CPU-heavy JS inside one
isolate.

## Sloppy Advantages

Sloppy is similar to Node/Bun/Deno at the JS execution level, but can do better at the
app-host level:

- native route match before JS;
- native request scopes;
- native data-provider scheduling;
- Sloppy Plan metadata;
- route timeouts/body limits/permissions known before handler;
- diagnostics know route/module/service context.

## Implementation Phases

- Phase 0: Documentation and ADR only.
- Phase 1: No event loop yet; core C primitives.
- Phase 2: Event loop abstraction skeleton.
- Phase 3: V8 bridge smoke; single isolate, single owner thread.
- Phase 4: Handwritten handler execution.
- Phase 5: Async Promise settlement model.
- Phase 6: Native worker pool abstraction.
- Phase 7: DB provider async strategy.
- Phase 8: HTTP integration with request scopes.
- Phase 9: Cancellation/deadlines/backpressure.
- Phase 10: Multiple workers/isolates.

## Testing Requirements

- Unit tests for request scope lifetime later.
- Native SlAsync settlement tests now; V8 Promise, microtask, and request-scope settlement
  tests later.
- Cancellation cleanup tests later.
- Worker pool no-V8-entry tests later.
- Resource leak tests.
- Async DB transaction rollback tests.
- Stress test for many pending requests later.
- Concurrency docs acceptance tests may be manual until implementation exists.

## Acceptance Criteria

For this spec pass:

- `docs/concurrency.md` exists.
- ADR exists.
- Architecture/execution/memory/data/performance docs link to it.
- Roadmap has concurrency epic.
- V8 threading rule is clear.
- No runtime code added.

For future implementation:

- One isolate has one owner thread.
- Worker threads cannot call into V8.
- Promise-returning handler keeps request scope alive.
- Rejected promise produces route-aware diagnostic.
- Request cancellation disposes scoped resources.
- Stress tests show many pending async operations do not create thread-per-request behavior.
