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
V8 feature-gated namespace metadata, docs, and examples.

Implemented in the bootstrap stdlib:

- `BackgroundService.create(...)` lifecycle resource registration through `app.use(...)`.
- bounded `WorkQueue` FIFO admission, concurrency, overflow, drain, shutdown, cancellation,
  timeout, retry, and unsupported-payload behavior.
- `WorkerPool.create(...).run(...)` bounded worker-pool style admission over the current
  JavaScript bootstrap execution layer.
- `Worker.start(...).invoke(...).stop()` bootstrap module execution and stale-handle checks.

Explicitly not closed by this document unless bridge tests are added:

- production native CPU-parallel `WorkerPool` execution.
- separate V8 worker isolate startup and message passing in the runtime lane.
- resource-limit enforcement beyond validated API options and payload copy/serialization.

## Required Evidence

- bootstrap stdlib worker tests.
- feature/diagnostic unit tests.
- compiler import/Plan tests.
- doctor/audit goldens.
- source-input example shape checks.
- V8-gated evidence for `__sloppy.workers` metadata when the V8 lane is available.
