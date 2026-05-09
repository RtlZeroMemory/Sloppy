# Async Runtime Internals

## Where It Lives

- `include/sloppy/async.h`
- `include/sloppy/async_backend.h`
- `include/sloppy/loop.h`
- `include/sloppy/worker_pool.h`
- `src/core/async.c`
- `src/core/loop.c`
- `src/core/worker_pool.c`

## Model

The core async primitives define Sloppy-owned loop, completion, cancellation,
and worker-pool concepts. Platform backends provide implementation details
without leaking OS or libuv types into public core headers.

## Invariants

Cancellation and shutdown behavior must be explicit. Hidden global mutable
runtime state is not allowed.
