# Concurrency Module

## Status

Partially implemented for TASK 09.A and TASK 09.B.

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

Future scope still includes real event-loop backends, V8 Promise integration, microtask
draining, request lifetime, cancellation tokens, deadlines, backpressure, thread-safe
posting, and worker pool boundaries.

## Non-goals

No libuv backend, OS event loop, timers, sockets, HTTP, worker pool, threads, atomics,
locks, V8 Promise integration, V8 microtask draining, JS async handler execution, request
lifecycle, cancellation token, deadline, or backpressure behavior in TASK 09.B.

## Public/Internal API

Implemented public header:

- `include/sloppy/loop.h`
- `include/sloppy/async.h`

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

Planned worker abstractions remain future work.

## Ownership/Lifetime Rules

`SlLoop` owns completion registrations only. It does not own callback payloads or user data;
those pointers are borrowed by the loop and may be NULL.

Completion storage is caller-owned. `sl_loop_init` accepts a caller-provided
`SlCompletion` array and fixed capacity. Zero-capacity loops are valid but cannot accept
posts.

Future request scopes live until promise settlement, response completion, or cancellation
cleanup.

`SlAsync` is caller-owned and does not allocate. It owns only the settlement record fields.
Payload, result user data, continuation user data, and diagnostics are borrowed. Borrowed
diagnostics must remain valid until loop dispatch; real V8/request scope integration will
define a stronger ownership contract later.

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
- double settlement fails with `SL_STATUS_INVALID_STATE`;
- failed loop posting leaves the async object pending and without a stored result;
- continuation failure propagates through the loop dispatch call.

Concurrency invariants:

- this skeleton is single-threaded and does not enforce owner-thread identity yet;
- cross-thread posting is not supported yet;
- cross-thread async settlement is not supported yet;
- native worker threads must never call JS directly;
- V8 isolates must only be entered by their owning JS event-loop thread.

## Diagnostics

TASK 09.A loop APIs and TASK 09.B async APIs return `SlStatus` only and do not emit
diagnostics. `SlAsync` may borrow an existing diagnostic for rejected/cancelled settlement,
but it does not build, copy, render, or own diagnostics. V8 Promise rejection,
route/request-aware cancellation, overload, and wrong-thread diagnostics remain later work.

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
- NULL loop and NULL async settlement rejection;
- continuation failure propagation through loop drain;
- deterministic completion order across multiple async objects.

V8 Promise integration, V8 microtasks, request-scope retention, cancellation cleanup,
thread-safe posting, libuv/backend behavior, and no-V8-entry worker tests remain future
work.

## Source Docs

- `docs/concurrency.md`;
- `docs/execution-model.md`;
- `docs/memory.md`;
- `docs/testing-strategy.md`;
- ADR 0014.

## Open Questions

- Exact event loop backend integration.
