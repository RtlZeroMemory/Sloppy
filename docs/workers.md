# Workers, Background Tasks, Queues, and CPU Offload

## Status

CORE-WORKER-01 introduces the public `sloppy/workers` module, feature metadata, diagnostics,
doctor/audit evidence, examples, and bootstrap tests for worker-shaped resources. The shipped
JavaScript bootstrap API implements deterministic `BackgroundService`, bounded `WorkQueue`,
and `WorkerPool` admission semantics. `Worker.start()` can execute explicit worker modules in
the bootstrap/Node test lane and is bridge-gated in the V8 runtime lane.

Native V8 worker isolates and true native CPU offload are not reported as production-ready
performance features. The V8 bridge exposes only feature-gated `__sloppy.workers` metadata
until native worker-isolate execution grows a real bridge. Do not close issue scope that
requires true separate V8 isolates or native CPU parallelism unless those bridge tests exist.

## Public API

```ts
import {
  BackgroundService,
  WorkQueue,
  WorkerPool,
  Worker
} from "sloppy/workers";
```

- `BackgroundService.create(name, handler, options?)` creates a lifecycle-bound app resource.
- `WorkQueue.create(name, options)` creates a bounded in-process FIFO queue.
- `WorkerPool.create(name, options)` creates a bounded offload-style queue with fixed worker
  concurrency.
- `Worker.start(modulePath, options?)` starts an explicit worker module when a supported bridge
  or bootstrap module loader is available.

All resource names must be stable non-empty strings. Public JavaScript handles expose no raw
native pointers, OS handles, libuv handles, thread IDs, or V8 isolate pointers.

## Lifecycle

`BackgroundService` resources are usable through `app.use(service)`. The app-host bootstrap
starts services when they are registered and records worker resource metadata in
`app.__getPlanContributions().workers`. Service shutdown cancels the service signal and waits
for the service handler to observe cancellation or complete.

`WorkQueue`, `WorkerPool`, and `Worker` are deterministic runtime resources:

- `stop({ drain: true })` rejects new admission and lets queued/running work finish.
- `stop({ drain: false })` rejects new admission and cancels queued work.
- stale handles reject with `SLOPPY_E_WORKER_STALE_HANDLE`.
- late completion after timeout/cancellation is cleanup-only from the caller's perspective.

## Queue Policy

Queues are bounded by default. `WorkQueue.create()` defaults `maxQueued` to 1024 and
`concurrency` to 1. `WorkerPool.create()` defaults `maxQueued` to 64 and `workers` to 1.
Overflow policy is explicit:

- `overflow: "reject"` fails admission with `SLOPPY_E_WORK_QUEUE_FULL`.
- `overflow: "backpressure"` waits only up to the bounded waiter capacity.

Jobs receive copied/serialized payloads. Supported payloads are `null`, booleans, finite
numbers, strings, arrays, plain objects, `ArrayBuffer`, and typed-array views. Unsupported
payloads fail deterministically without including payload values in diagnostics.

Retry policy is explicit through `retry.maxAttempts` and `retry.backoffMs`. Exhausted retry
attempts fail with `SLOPPY_E_WORK_RETRY_EXHAUSTED`. Cancellation and deadline expiration are
separate: cancelled work uses `SLOPPY_E_WORK_JOB_CANCELLED`; timed-out work uses
`SLOPPY_E_WORK_JOB_TIMEOUT`.

## Feature And Plan Metadata

Compiler imports from `sloppy/workers` emit:

- `requiredFeatures: ["stdlib.workers"]`
- `features.workers: true`
- `strongPlan.evidence.workers: true`
- a `workers.policy` object describing bounded defaults, no raw native handles, and owner-thread
  settlement requirements
- `doctorChecks[].id = "stdlib.workers.contract"`

Runtime feature activation registers `stdlib.workers` with dependencies on `core`, `v8`,
`transport.libuv`, `stdlib.time`, and `stdlib.codec`.

## Diagnostics

Stable worker diagnostics include:

- `SLOPPY_E_WORKERS_FEATURE_UNAVAILABLE`
- `SLOPPY_E_BACKGROUND_SERVICE_START_FAILED`
- `SLOPPY_E_BACKGROUND_SERVICE_FAILED`
- `SLOPPY_E_WORK_QUEUE_FULL`
- `SLOPPY_E_WORK_QUEUE_STOPPED`
- `SLOPPY_E_WORK_JOB_CANCELLED`
- `SLOPPY_E_WORK_JOB_TIMEOUT`
- `SLOPPY_E_WORK_JOB_FAILED`
- `SLOPPY_E_WORK_RETRY_EXHAUSTED`
- `SLOPPY_E_WORKER_POOL_UNAVAILABLE`
- `SLOPPY_E_WORKER_POOL_SATURATED`
- `SLOPPY_E_WORKER_CRASHED`
- `SLOPPY_E_WORKER_RESOURCE_LIMIT_EXCEEDED`
- `SLOPPY_E_WORKER_MESSAGE_SERIALIZATION_FAILED`
- `SLOPPY_E_WORKER_ISOLATE_STARTUP_FAILED`
- `SLOPPY_E_WORKER_UNSUPPORTED_PAYLOAD`
- `SLOPPY_E_WORKER_SHUTDOWN_CANCELLED`
- `SLOPPY_E_WORKER_STALE_HANDLE`

Diagnostics, doctor output, audit output, examples, and goldens must not include payload values
or secrets.
