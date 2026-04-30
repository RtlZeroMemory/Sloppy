# TASK ENGINE-12.AB: Libuv Async Backend and V8 Continuation Scheduler

## Issues

- #307 / TASK ENGINE-12.A: Native Event Loop and Completion Queue Backend
- #308 / TASK ENGINE-12.B: Owner-Thread V8 Continuation Scheduler
- Contributes to #306 / EPIC ENGINE-12: Scalable Async Runtime

## Implemented In This Slice

- Slop-owned `SlAsyncLoop` and `SlAsyncCompletion` abstractions with bounded capacity.
- Deterministic test backend for default unit tests.
- Libuv backend under `src/platform/libuv/` with thread-safe async wakeup/posting.
- Completion cleanup-once behavior and explicit scope retain/release hooks.
- V8-internal native continuation scheduler that settles Promises only on the engine owner
  thread.

## Explicit Non-Goals

- No Node compatibility.
- No public timers, fetch, fs, process, Buffer, or libuv API.
- No provider offload or HTTP backend rewrite.
- No full cancellation/deadline/shutdown drain policy; #309 owns that.
- No scalability/backpressure evidence or performance claims; #310 owns that.
