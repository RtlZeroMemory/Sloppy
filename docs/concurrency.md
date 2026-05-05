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
- No CPU-parallel JS execution in a single isolate.
- No Node/libuv compatibility promise, and no public timers/fetch/fs/process APIs.

## Current Implementation

HTTP-25.A/B/C update: keep-alive in the current transport is sequential only. One
connection may carry multiple HTTP/1.1 requests, but a second request is not dispatched
until the first response write completes and request-owned state is reset. Idle
keep-alive connections are bounded by an idle timer, per-connection request count is
bounded, and shutdown closes idle keep-alive connections before they can accept new work.
HTTP-25.D/E keeps that model: chunked request bodies are decoded into bounded full-body
storage before dispatch, and internal/native chunked streaming responses advance one
transport write at a time under a pending-byte cap. HTTP-25.F adds bounded repeated
operation evidence for keep-alive, chunked requests, streaming responses, backpressure, and
shutdown/cancel cleanup, but it is still stress/smoke evidence rather than a performance or
production-drain claim. There is still no pipelining, concurrent request execution on one
connection, request streaming API, public JS streaming response helper, SSE/WebSockets,
file streaming, or production graceful-drain claim.

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

ENGINE-12.AB adds the first real async backend foundation beside the older skeletons.
`include/sloppy/async_backend.h` defines an opaque `SlAsyncLoop` over caller-provided
completion storage. The loop takes ownership only of completions that are successfully
queued; deterministic overflow reports `SL_STATUS_CAPACITY_EXCEEDED` without taking
ownership. Queued completions have cleanup-once ownership and optional scope
retain/release hooks for request/app lifetime retention. The deterministic test backend is
available for default unit tests. The primary
runtime backend is libuv, implemented under `src/platform/libuv/`; `uv_loop_t`,
`uv_async_t`, `uv_handle_t`, and other libuv types do not appear in public Sloppy headers
or JavaScript/framework contracts. Libuv is used for internal cross-thread wakeup/posting,
not as a public API model.

ENGINE-03 adds the first real V8 async handler boundary. Returned Promises that settle
during the explicit owner-thread V8 microtask drain fulfill or reject deterministically;
fulfilled values use normal result conversion, rejected Promises produce engine
diagnostics, and still-pending Promises fail as deadline-style handler failures instead of
being serialized as `[object Promise]` or reported as success. ENGINE-03 also exposes a
native cancellation snapshot to request context and covers cleanup for the bounded
microtask call path.

ENGINE-03 is not the full scalable async runtime. It does not add a Node-style event loop,
public timers/fetch/fs/process APIs, native provider completion queues, cross-thread
posting, HTTP disconnect/shutdown drain behavior, worker-thread scheduling, or scalability
evidence. The full async-runtime target is tracked by #306 and tasks #307 through #310.
ENGINE-12.AB owns the first native completion/backend integration and owner-thread
continuation boundary. ENGINE-23.A/B adds the provider descriptor/admission foundation
without converting SQLite or adding public async APIs. `include/sloppy/async_backend.h`
classifies native completions as internal, nonblocking I/O, blocking offload, timer, or
provider operations. `include/sloppy/provider_executor.h` defines Slop-owned provider
execution modes and a bounded per-provider-instance executor abstraction. ENGINE-23.C adds
the `SERIALIZED_BLOCKING` worker lifecycle: one long-lived worker per provider instance,
one active operation at a time, FIFO activation, completion posting through `SlAsyncLoop`,
and cleanup-once behavior for success, failure, cancellation, timeout, overflow, shutdown,
dispose, and late completion. ENGINE-23.D adds `BLOCKING_POOL` for provider instances that
can safely parallelize blocking work: the pool starts a bounded number of long-lived
workers, caps in-flight work at configured capacity no larger than worker count, keeps a
bounded per-instance queue, and rejects overflow before ownership transfer. ENGINE-23.E/F
adds capability-gated admission and terminal-state policy for both worker modes: capability
denial, pre-cancellation, expired-deadline state, overflow, and shutdown all happen before
provider work starts when possible; active cancellation/timeout/shutdown posts terminal
state while the blocking native callback is allowed to return later. Neither mode wires
SQLite through async offload or makes benchmark claims.

ENGINE-23 is the provider execution runtime layer after ENGINE-12. It owns production
provider operation descriptors, per-provider-instance executors, serialized SQLite-class
blocking offload, bounded blocking pools, nonblocking provider mode, capability-gated
admission, cancellation/deadline/shutdown/late-completion semantics, worker lifecycle,
diagnostics, and stress evidence. Provider execution is separate from ENGINE-13 HTTP
backend work and must not use libuv's global threadpool as the provider runtime.

ENGINE-13.A/B/C adds the first HTTP-specific lifecycle/admission layer above the generic
async primitives. `SlHttpBackend` tracks bounded active connection and active request
counts, `SlHttpRequestLifecycle` owns one request admission slot and a borrowed request
arena, and timeout hooks cancel through `SlCancellationToken` with
`SL_CANCELLATION_REASON_DEADLINE_EXCEEDED`. ENGINE-13.D/E adds the HTTP-specific bounded
body reader and shutdown/cancel policy on top of that state: body chunks are copied only up
to the configured request limit, cancellation/deadline/shutdown are checked before and
during body reads, new request work is rejected once backend shutdown begins, and active
requests can be cancelled through the shutdown hook with cleanup-once admission release.
This is still not a real timer integration, client-disconnect signal, socket drain timer,
or stress evidence; those remain ENGINE-13.F/#324 and later platform/backend follow-ups.

ENGINE-24.A/B adds the first reusable HTTP transport listener under the platform boundary.
The libuv TCP listener runs inside `src/platform/libuv/`, accepts sockets into a bounded
Slop-owned connection table, and parks accepted connections in `ACCEPTED` state. It does
not start a request read loop, parse socket chunks, dispatch routes, write responses, use
libuv's global threadpool, enter V8, or claim keep-alive/graceful-drain behavior. Later
ENGINE-24 slices must post/read/write/cancel through the documented owner-loop and request
lifetime rules before claiming scalable async HTTP behavior.

ENGINE-24.C starts the accepted-connection libuv read loop inside the same platform
boundary. Read callbacks append TCP chunks into fixed per-connection storage through
Slop's bounded byte-builder primitive, detect a complete head/body for one request, and
then call the existing ENGINE-13 parser/body-reader semantics. The read callback never
enters V8, never dispatches routes, never writes a response, and stops reading once the
connection reaches request-ready. Client disconnect/read failure during head or body closes
the transport connection through cleanup-once paths and releases backend request/connection
admission without exposing libuv handles or native pointers.

ENGINE-24.D consumes request-ready state on the same libuv owner loop when a dispatch
callback is configured. Dispatch transitions backend request state to dispatching, response
serialization uses the existing HTTP response writer, and libuv writes from
connection-owned response storage that remains valid until the write callback. The write
callback performs lifecycle completion and close cleanup only; it must not enter V8. The
MVP policy remains one request per connection, close-after-response, no keep-alive, no
pipelining, and no streaming response body.

ENGINE-24.E adds the first real transport cancellation/timeout/shutdown hooks for that
libuv-backed server. Header-read, body-read, total-request, and write timeouts are libuv
timers owned by the transport connection on the owner loop. Timer callbacks, read
disconnects, and write callbacks never enter V8; they mark the request/connection terminal,
cancel or time out the backend request lifecycle when one exists, release counters exactly
once, and treat late callbacks after terminal close as cleanup-only. Server stop uses an
immediate-cancel/drain-lite policy: stop accepting, reject new accepted work, cancel active
request lifecycles through the backend shutdown path when present, close idle/reading/
dispatching/writing transport connections, and drain close callbacks. This is not
production graceful drain, keep-alive idle management, or scalable async HTTP evidence.

ENGINE-24.G makes the keep-alive decision explicit: the ENGINE-24 transport MVP is
close-after-response, one request per connection, no sequential second request, no
pipelining, and no streaming response body. Future HTTP/1.1 keep-alive must resume the read
loop only after response write completion, own an idle timeout and max-requests cap, reset
parser/body/request arena state between requests, keep per-request cancellation separate
from the longer-lived TCP connection, and define shutdown drain behavior for idle and
active keep-alive connections before Slop claims keep-alive support.

Implement the full scalable async runtime when a real external async source is ready to
wire end-to-end, such as HTTP disconnect/shutdown cancellation, timer/deadline wakeups,
async SQLite/provider work, or worker-pool offload. It is also required before Sloppy makes
any public or alpha claim about scalable async behavior, production-ready async HTTP
lifecycle, async provider execution, or performance with many pending requests.

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
- posts native completions into a bounded Slop-owned completion queue.
- ENGINE-12.AB uses libuv internally as the primary backend and keeps the deterministic
  test backend for unit tests. This is not Node compatibility and does not expose timers,
  fetch, fs, process, or libuv handles to JavaScript.

Native worker pool:

- runs blocking or CPU-heavy native operations;
- never enters V8 directly;
- posts completion back to the JS event loop.
- provider blocking work uses ENGINE-23 Slop-owned provider executors rather than the
  inline TASK 09.C skeleton or libuv's global threadpool.

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
but that skeleton remains single-threaded. ENGINE-12.AB adds `SlAsyncLoop`, a bounded
backend abstraction with a libuv implementation that supports cross-thread completion
posting and owner-thread drain checks. Completion callbacks still run only when the owner
drains the loop; native worker/provider code may post completion data but must not enter
V8.

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
not implemented in the skeleton. The real async foundation must include cancellation,
deadlines, and backpressure from the beginning of implementation.

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
Cross-thread native completion posting exists at the `SlAsyncLoop` boundary. Full
worker-pool/offload policy, cancellation/deadline propagation, shutdown drain/cancel, and
backpressure remain future ENGINE-12 tasks. Current `SlLoop` callbacks still run on the
loop caller thread, so the older skeleton must not be used to bypass the V8 isolate
owner-thread rule.

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

ENGINE-16.A/B wires the dev app host's native request scopes to the app lifecycle when a
request enters the Plan-backed dispatch path. Opening a request scope requires the app
lifecycle to be running, records app/request identity, and increments the lifecycle's
active-request count. Shutdown begins by rejecting new request scopes; graceful finish is
allowed only after active request scopes close, while forced shutdown closes app resources
immediately under the current dev-only policy. This does not add a production drain,
distributed lifecycle, thread-per-request model, or provider expansion.

## Promise and Microtask Handling

After calling into JS, the engine bridge must drain or schedule microtasks according to
runtime policy. Rejected promises become diagnostics. Request scope remains alive while a
promise is pending. Promise settlement triggers response or error handling. Unhandled
rejections should include route and handler context when possible.

TASK 09.B does not implement this V8 behavior. It only defines the native settlement record
and loop-continuation shape that future V8 Promise resolution can map onto or evolve.
ENGINE-03 implements the first V8 bridge cut for microtask-only Promise settlement:
handlers are invoked only on the isolate owner thread, V8 microtasks drain explicitly after
app evaluation and handler calls, fulfilled Promises convert through the normal result
path, rejected Promises become deterministic diagnostics, and Promises that remain pending
after the bounded microtask drain fail as a deadline-style handler failure. This prevents
`[object Promise]` success and silently dropped asynchronous failures without claiming a
Node event loop, timers, fetch, filesystem APIs, or native async provider completion
queues.

## Request Scope Lifetime

The request arena exists until response completion or cancellation cleanup. Scoped services
live until the request finishes. DB transactions/resources tied to the request must
close/rollback/dispose on cancellation or error. Data crossing async boundaries must not
point into shorter-lived arenas. Debug builds should detect leaked request resources.

## Native Async Operations

Sloppy distinguishes native async operation kinds at the runtime boundary:

- internal completions: runtime-owned bookkeeping and owner-thread continuations;
- real nonblocking I/O: socket/readiness/provider APIs that do not occupy a worker while
  waiting and are driven by the event backend;
- blocking offload: blocking native work that must run away from the V8 owner thread;
- timers/deadlines: backend timer wakeups that create deterministic timeout completions;
- provider operations: database/provider work admitted through Slop provider executors.

Real nonblocking I/O is driven by the event backend. Blocking provider/offload work is
admitted through Slop-owned executors. Both complete through `SlAsyncCompletion`, and
JavaScript sees only Promises, results, and errors. It never sees libuv handles, worker
threads, queue slots, or native provider operation pointers.

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

EPIC-16's native SQLite provider is synchronous and used only by C/runtime tests. It does
not run blocking work on `SlWorkerPool`, does not settle JavaScript promises, and does not
change the future requirement that JS-visible database completions post back to the owning
event loop.

Transactions pin their connection/resource until the async callback settles. Completions
post back to the JS event loop. Providers must support cancellation/deadline where possible,
or document when they cannot.

Provider execution modes are Slop-owned policy, not libuv policy:

- `INLINE_FAST`: bounded metadata/config work only. It must not perform disk, network, DB,
  lock, or other blocking waits.
- `SERIALIZED_BLOCKING`: implemented as one long-lived worker and one active operation at
  a time for one provider instance. This is the default policy for a single SQLite
  connection unless a later provider-specific issue chooses different read/write
  semantics.
- `BLOCKING_POOL`: implemented as a bounded worker pool for one provider instance when the
  provider is documented as safe to parallelize blocking calls. It does not create one
  thread per request; worker count, queue capacity, and in-flight count are fixed by the
  provider instance configuration.
- `NONBLOCKING_IO`: true async provider/client operation through event backend readiness or
  provider async APIs. No provider worker is occupied while waiting.
- `EXTERNAL_MANAGED`: a future escape hatch for externally managed runtimes or pools. It
  still must use Slop admission, completion, diagnostics, and lifetime contracts.

Provider instances are named runtime resources such as `sqlite:main`, `sqlite:audit`,
future `postgres:main`, or future `sqlserver:reporting`. Each instance owns or references
an executor policy: mode, queue capacity, in-flight count, optional worker count, shutdown
state, and simple counters. There is no unbounded global provider queue, no
thread-per-request behavior, and no hidden dependency on libuv's global threadpool as the
provider runtime.

Provider operation descriptors must own every byte needed after submission. SQL strings,
parameter strings/blobs, provider config references needed by queued work, diagnostic
context, and completion targets must be copied, retained, or represented by a safe resource
ID. Borrowed request-arena views may not be queued unless the request/app scope lifetime is
explicitly retained. Operation cleanup runs exactly once; late completion after
cancellation, timeout, or shutdown is cleanup-only and must not double-settle. Provider
worker code must never enter V8. The current serialized worker posts through the libuv
async backend; the deterministic test backend remains single-threaded and rejects attached
worker callbacks.

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

Cancellation means Slop stops accepting or using the result for that request/operation, and
cleanup still runs exactly once. It does not promise that an underlying blocking provider
call stops immediately. SQLite interruption, PostgreSQL cancellation, and SQL Server
cancellation are provider-specific future integrations. Late provider completion after
cancellation is ignored for settlement and used only for safe cleanup.

Timeout/deadline is distinct from caller cancellation. A timeout is a runtime deadline
terminal state and uses a deterministic deadline/timeout status and diagnostic. Timers are
owned by the backend layer when real timer wakeups are wired; the current ENGINE-12.CD
provider source exposes deterministic native timeout completion without public timer APIs.
Late completion after timeout is cleanup-only and must not double-settle.

The HTTP backend foundation has read/header/request timeout configuration fields and an
explicit request timeout hook. ENGINE-13.D/E makes body reads observe that request token:
pre-cancelled, cancelled-during-read, timed-out-during-read, and shutdown-during-read
requests stop body accumulation, discard late body bytes, release request admission exactly
once, and return deterministic cancellation/timeout/shutdown diagnostics. Real timer and
client-disconnect wakeups are still not wired; tests drive the terminal paths directly.

## Backpressure

Future model:

- limit request body size;
- limit pending requests per worker;
- limit worker-pool queue depth;
- limit DB pool checkout;
- streaming responses respect socket backpressure;
- future keep-alive connections have bounded idle time, bounded requests per connection,
  and explicit shutdown drain/force-close behavior;
- overload returns controlled errors instead of unbounded memory growth.

The default behavior for overload is deterministic rejection with diagnostics, not hidden
unbounded allocation. Provider executors have explicit per-instance queue capacity. A full
executor returns `SL_STATUS_CAPACITY_EXCEEDED`, records overflow, and does not take
operation ownership. Recovery is normal: once a queued operation completes and its cleanup
drains, the same provider instance can accept more work.

Provider executor shutdown is immediate-cancel in the current native executor: shutdown
stops new admission, posts shutdown terminal completions for pending and active operations,
and preserves cleanup-once behavior when completions drain and any claimed worker callback
returns. Already-running blocking worker callbacks are not forcibly interrupted by
ENGINE-23.E/F; their later result is treated as late completion and cannot double-settle.
Pending JS continuations resume only through the V8 owner-thread scheduler; provider
threads never resume JS.

HTTP backend admission follows the same bounded model: a full active connection or active
request budget returns `SL_STATUS_CAPACITY_EXCEEDED` with
`SLOPPY_E_HTTP_OVERLOAD`, and the backend does not create an unbounded request queue.
Body accumulation follows the same bounded model through `SlHttpBodyReader`: a body over
the configured byte limit fails before unbounded allocation, unsupported media fails before
handler dispatch, and body-reader failure discards partial request-body storage. Streaming
body APIs and socket write backpressure are still future ENGINE-13 work, so current claims
are limited to bounded body storage, admission counters, cancellation/shutdown terminal
paths, and deterministic rejection.

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
- Phase 5: ENGINE-03 V8-gated Promise settlement for the bounded owner-thread microtask
  boundary.
- Phase 6: Native worker pool abstraction.
- Phase 7: DB provider async strategy with cancellation-aware operation boundaries.
- Phase 8: HTTP integration with request scopes, cancellation, deadlines, and backpressure.
- Phase 9: ENGINE-12 scalable async runtime: native completion backend, owner-thread V8
  continuation scheduler, deadlines, shutdown drain/cancel, bounded queues, backpressure,
  provider/offload integration, and stress evidence.
- Phase 10: Hardening for provider-specific cancellation, deadline policy, and overload
  diagnostics.
- Phase 11: Multiple workers/isolates.

## Testing Requirements

- Unit tests for request scope lifetime later.
- Native SlAsync settlement tests now; ENGINE-03 adds V8-gated Promise, microtask,
  owner-thread, cancellation snapshot, and request-scope cleanup tests for the current
  microtask-only async handler boundary.
- ENGINE-12 must add broader cancellation cleanup tests with native async provider queues,
  HTTP disconnect handling, deadline wakeups, queue overflow, and shutdown drain/cancel
  behavior.
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
- Promise-returning handler keeps request scope alive for the bounded V8 microtask drain;
  future native completion queues must extend the same ownership rule across queued work.
- Rejected promise produces deterministic diagnostics; route/source remapping remains a
  later diagnostics quality layer where not already available.
- Request cancellation disposes scoped resources in the current request-scope execution
  path; future HTTP disconnect and shutdown cancellation paths must use the same token
  model.
- ENGINE-12 stress tests show many pending async operations do not create
  thread-per-request behavior before Sloppy claims scalable async performance.
