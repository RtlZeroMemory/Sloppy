# Concurrency Module

## Status

Partially implemented for TASK 09.A.

## Purpose

Define Sloppy's event loop, completion queue, owner-thread, promise settlement, and worker
pool model.

## Scope

Implemented now:

- `SlLoop` as a caller-backed, fixed-capacity native completion queue;
- synchronous `sl_loop_run_once` and `sl_loop_drain` dispatch;
- stop/reset behavior for deterministic single-threaded tests.

Future scope still includes real event-loop backends, promise settlement, request lifetime,
cancellation, backpressure, thread-safe posting, and worker pool boundaries.

## Non-goals

No libuv backend, OS event loop, timers, sockets, HTTP, worker pool, threads, atomics,
locks, V8 microtask draining, promise settlement, request lifecycle, cancellation,
deadline, or backpressure behavior in TASK 09.A.

## Public/Internal API

Implemented public header:

- `include/sloppy/loop.h`

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

Planned worker abstractions remain future work.

## Ownership/Lifetime Rules

`SlLoop` owns completion registrations only. It does not own callback payloads or user data;
those pointers are borrowed by the loop and may be NULL.

Completion storage is caller-owned. `sl_loop_init` accepts a caller-provided
`SlCompletion` array and fixed capacity. Zero-capacity loops are valid but cannot accept
posts.

Future request scopes live until promise settlement, response completion, or cancellation
cleanup.

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

Concurrency invariants:

- this skeleton is single-threaded and does not enforce owner-thread identity yet;
- cross-thread posting is not supported yet;
- native worker threads must never call JS directly;
- V8 isolates must only be entered by their owning JS event-loop thread.

## Diagnostics

TASK 09.A loop APIs return `SlStatus` only and do not emit diagnostics. Rejected promise,
cancellation, overload, and wrong-thread diagnostics remain later work.

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

Promise settlement, cancellation cleanup, thread-safe posting, libuv/backend behavior, and
no-V8-entry worker tests remain future work.

## Source Docs

- `docs/concurrency.md`;
- `docs/execution-model.md`;
- `docs/memory.md`;
- `docs/testing-strategy.md`;
- ADR 0014.

## Open Questions

- Exact event loop backend integration.
