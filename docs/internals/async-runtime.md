# Async runtime

Sloppy is single-owner-thread per V8 isolate. Native work — disk I/O,
sockets, provider drivers — runs off-thread but reports results back
to the owner thread, which is the only place JavaScript executes.

This page documents the rules. Most of them protect one invariant:
**JavaScript-visible state settles exactly once, on the owner thread**.

## Layout

```text
src/core/
  async_backend.c               readiness/wakeup driver (libuv-backed)
  async_backend_internal.h
  loop.c                        owner-thread run loop
  cancellation.c                signal sources, shared signal model
  scope.c                       lifetime/cleanup containers

src/engine/v8/
  engine_v8.cc                  microtask drain, Promise settlement
  intrinsics_*                  JS↔native call/return marshalling

src/platform/libuv/             socket/timer/process readiness
src/data/                       provider-specific async state machines

stdlib/sloppy/
  workers.js                    cancellation primitives, queues, pools
  time.js                       deadline + cancellation surfaces
```

## Owner threads

Every V8 isolate has one owner thread. The bridge stamps the thread on
isolate creation; every entry point asserts it before touching V8.

| Isolate                | Owner thread                         |
| ---------------------- | ------------------------------------ |
| Main app isolate       | Main run-loop thread                 |
| Per-worker isolate     | That worker's thread                 |

Native completions originating off-thread don't enter the isolate
directly. They post a completion record through the async backend and
the owner thread picks it up during its run loop.

## End-to-end shape

```text
JS handler awaits db.query(...)
   │  V8 owner thread
   ▼
provider intrinsic submits an async op + Promise
   │  src/engine/v8/intrinsics_*
   ▼
provider executor admits, picks mode, kicks driver
   │  src/core/provider_executor.c → src/data/<provider>.c
   ▼
driver progress drives readiness via async backend
   │  src/core/async_backend.c (libuv polling)
   ▼
completion record produced (off-thread or on-thread)
   │
   ▼
owner thread run loop notices, runs continuation
   │  src/core/loop.c
   ▼
intrinsic resolves/rejects the awaiting Promise
   │  V8 owner thread (microtask drain)
   ▼
JS handler resumes
```

## Cancellation

`SlCancellationToken` (`include/sloppy/cancellation.h`) is the native
cancellation primitive. JS exposes it through
`WorkerCancellationController` / `WorkerCancellationSignal`
(`stdlib/sloppy/workers.js`).

A signal carries:

- `cancelled` — terminal flag (cannot reverse)
- `reason` — opaque value supplied at cancel time
- `addEventListener("abort", fn)` — for consumers that need to react

Any operation that takes `{ signal }` reads it before submission and
honors the cancelled state immediately:

```text
if (signal.cancelled) → fail with cancellation error, no submission
if signal cancels mid-flight → cancel driver work where supported,
                               release admission slot, settle Promise
```

The same surface is composed for deadlines: `{ deadline }` and
`{ timeoutMs }` are sugar that builds an internal token.

## Late completions

A late completion is when native work returns *after* its caller has
already given up (deadline expired, signal cancelled, request scope
ended).

The rule: **late completions only run cleanup**.

- The Promise has already settled with a cancellation/timeout error.
- The driver result is freed by the provider's cleanup.
- Any per-operation arena allocation is released to the request arena.
- Nothing JS-visible is updated.

This is enforced by the executor and by scope-bound cleanup
registration. Code reviewers look for "settle once" wherever a
completion record could race with cancellation.

## Scopes and cleanup

Every async operation is owned by *some* scope:

- **Request scope** — created at HTTP dispatch, ends after response
  write or terminal failure.
- **App scope** — startup to shutdown; owns singletons and
  background-service handles.
- **Worker scope** — per worker isolate.

Cleanup runs in reverse-registration order at scope end. A cleanup is
always called exactly once: scope end, cancellation, or shutdown — pick
the first to fire.

## V8 microtask drain

The bridge drains V8 microtasks during `engine_dispatch` after a
handler returns. The drain is bounded (configured per dispatch) — long
chains of awaits can't keep the dispatcher hostage.

What this means for handlers:

- Returning a Promise settles within the drain window or the dispatch
  reports a deterministic failure.
- A handler that awaits multi-second background work should queue it
  via `WorkQueue` and return `Results.accepted({ jobId })`, not block
  the dispatch.

This is intentionally not a Node-style event loop. There is no
`setTimeout` that survives across requests, no global pending-async
state. Every async operation is rooted in a scope.

## Worker isolates

`WorkerPool` and `Worker` allocate per-worker V8 isolates. Each has its
own owner thread. Cross-worker communication is structured (typed
messages); workers don't share heap state.

Worker shutdown drains in-flight messages within their deadlines, then
disposes the isolate. A worker that's stuck in an infinite loop is
forced down at shutdown timeout.

## Shutdown ordering

```text
1. transport: stop accepting new connections
2. transport: stop accepting new requests on existing connections
3. wait for in-flight requests to complete or hit deadlines
4. drain provider runtime: cancel in-flight ops, close pools
5. shut down workers
6. shutdown engine bridge (release isolates)
7. release app arena
```

Each step has its own timeout. The runtime never wedges on shutdown —
forced cleanup runs after the deadline.

## Tests

- **V8 microtask bound** unit tests cover bounded-drain semantics.
- **Provider cancellation** tests cover deadline and signal handling
  per provider.
- **Late-completion** tests assert cleanup-only behavior.
- **Stress / torture** lanes (opt-in) hammer cancellation/late
  completion paths under load.

## Not implemented

- Node-style global event loop (timers, intervals, immediates spanning
  request scopes).
- Public streaming response/request APIs.
- Cross-worker shared memory.
- A fetch-style `AbortSignal` wired into HTTP request lifecycles —
  cancellation today flows through provider/worker APIs that take
  `{ signal }` explicitly.
