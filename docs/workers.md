# Workers, Background Tasks, Queues, and CPU Offload

## Status

CORE-WORKER-01 introduces the public `sloppy/workers` module, feature metadata, diagnostics,
doctor/audit evidence, examples, bootstrap tests, and V8-gated worker bridge coverage. The
JavaScript bootstrap API implements deterministic `BackgroundService`, bounded `WorkQueue`,
and `WorkerPool` admission semantics. In V8 builds, `WorkerPool.run(...)` runs copied work in
a separate worker-owned V8 isolate and settles its Promise on the owning isolate thread.
`Worker.start()` loads an explicit worker module, invokes exported functions through copied
messages, and exposes no raw native handles.

This is correctness and lifecycle evidence, not a benchmark or throughput claim. Default
non-V8 gates still do not prove V8 isolate execution; report the V8 lane separately.

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
- `WorkerPool.create(name, options)` creates a bounded offload queue with fixed worker
  concurrency; the V8 bridge executes admitted work in worker-owned isolates.
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
- pending backpressure waiters are rejected on stop because they have not been queued.
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

The V8 bridge copies input as serialized text into the worker isolate and copies serialized
results back before owner-thread settlement. Worker modules use Sloppy-owned source files with
`export function`, `export async function`, or `export const` exports; this is not Node
`worker_threads`, Web Worker compatibility, or package-manager resolution.

Resource limits are scoped to bounded queues, payload/result byte caps, module source caps,
and `memoryLimitMb` validation. Exceeding those limits fails with
`SLOPPY_E_WORKER_RESOURCE_LIMIT_EXCEEDED`.

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
