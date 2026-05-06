# CORE-WORKER-01 Workers, Background Tasks, Queues, and CPU Offload API

## Scope

Parent issue: #632.

Child issues:

- #633 API contract and lifecycle policy.
- #634 feature/Plan/diagnostics/capability model.
- #635 BackgroundService and app lifecycle.
- #636 WorkQueue and bounded processing.
- #637 retry/failure/backpressure/shutdown.
- #638 WorkerPool CPU/native offload.
- #639 JS worker isolates and message passing.
- #640 transfer/serialization/resource limits.
- #641 V8/stdlib JS surface.
- #642 doctor/audit/conformance/examples/docs/goldens.

## Implementation Status

This slice lands the public `sloppy/workers` module, runtime feature metadata,
worker-specific diagnostics, compiler Plan metadata, doctor/audit goldens, bootstrap tests,
V8 worker bridge tests, docs, and examples.

Implemented in the bootstrap stdlib:

- `BackgroundService.create(...)` lifecycle resource registration through `app.use(...)`.
- bounded `WorkQueue` FIFO admission, concurrency, overflow, drain, shutdown, cancellation,
  timeout, retry, and unsupported-payload behavior.
- `WorkerPool.create(...).run(...)` bounded admission and, in the V8 lane, execution in a
  worker-owned isolate with owner-thread Promise settlement.
- `Worker.start(...).invoke(...).post(...).stop()` bootstrap module execution, V8 worker
  isolate invocation/message passing, resource-limit validation, and stale-handle checks.

## Required Evidence

- bootstrap stdlib worker tests.
- feature/diagnostic unit tests.
- compiler import/Plan tests.
- doctor/audit goldens.
- source-input example shape checks.
- V8-gated evidence for `__sloppy.workers` metadata, WorkerPool offload, worker isolate
  start/invoke/post/stop, stale-handle behavior, and scoped resource-limit failure.
