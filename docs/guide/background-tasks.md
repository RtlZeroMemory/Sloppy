# Background tasks

This page covers the way Sloppy apps run work alongside HTTP request handling:
long-running services, queued jobs, and worker isolates. The runtime primitives
live in `sloppy/workers`; the public Core APIs are documented at
[Workers](../api/workers.md) and the reference table at
[Reference / Workers](../reference/workers.md).

Pick the primitive that matches the shape of the work:

| Primitive | When to use |
| --- | --- |
| `BackgroundService` | App-lifetime tasks (cleanup loops, periodic flushes, drains) |
| `WorkQueue` | Bounded queued jobs with concurrency, retry, and backpressure |
| `WorkerPool` | CPU-bound or untrusted work offloaded to a bounded set of worker isolates |
| `Worker.start(...)` | A single explicit worker isolate from a module path |

> Scheduled / cron-style jobs are not a separate public API in this alpha. Use
> a `BackgroundService` that sleeps with `Time.delay(ms, { signal })`, or a
> `WorkQueue` that you enqueue from your own trigger. A scheduler surface is
> tracked separately and is not promised here.

## Background services

A `BackgroundService` is a long-running async task with start/stop semantics.
Register it with `app.use(...)` — the app starts the service immediately, and
the handler receives a context object with the cooperation `signal`:

```ts
import { Sloppy, Results } from "sloppy";
import { Time } from "sloppy/time";
import { BackgroundService } from "sloppy/workers";

const app = Sloppy.create();

const cleanup = BackgroundService.create("cleanup", async (ctx) => {
    while (!ctx.signal.cancelled) {
        await Time.delay(5 * 60 * 1000, { signal: ctx.signal });
        // periodic work here
    }
});

app.use(cleanup);

app.mapGet("/", () => Results.text("cleanup service registered"));

export default app;
```

Handle methods and state:

| Member | Purpose |
| --- | --- |
| `service.start()` | start (idempotent until completion) |
| `service.stop(reason?)` | request stop; resolves once the handler returns |
| `service.state` | `"created" \| "running" \| "stopped" \| "completed" \| "failed"` |
| `service.failure` | `SloppyWorkerError` when state is `"failed"` |

App shutdown does not yet auto-call `service.stop(...)`. If you need a clean
stop, hold a reference and call `service.stop(...)` from your shutdown path
(for example, a process-level signal handler). Track this surface against the
[stability matrix](../reference/stability.md#feature-matrix).

## Work queues

`WorkQueue.create(name, options?)` returns a bounded producer/consumer queue.
You register a consumer with `queue.process(handler)` and enqueue jobs from
routes (or anywhere) with `queue.enqueue(data, options?)`:

```ts
import { Sloppy, Results } from "sloppy";
import { WorkQueue } from "sloppy/workers";

const app = Sloppy.create();

const emails = WorkQueue.create("emails", {
    maxQueued: 1000,
    concurrency: 4,
    overflow: "reject",
    retry: { maxAttempts: 3, backoffMs: 0 },
});

emails.process(async (job, ctx) => {
    // job.id, job.data, job.attempt; ctx.signal, ctx.deadline, ctx.attempt
    ctx.signal.throwIfCancelled?.();
    await sendEmail(job.data);
    return { sent: true };
});

app.use(emails);

app.mapPost("/emails/welcome", async () => {
    await emails.enqueue({ to: "user@example.com", template: "welcome" });
    return Results.accepted({ queued: true });
});

export default app;
```

`WorkQueue.create` options:

| Option | Default | Notes |
| --- | --- | --- |
| `maxQueued` | `1024` | bound on queued work |
| `concurrency` | `1` | active jobs at once |
| `overflow` | `"reject"` | `"reject"` or `"backpressure"` when full |
| `maxBackpressureWaiters` | `maxQueued` | cap on `backpressure` waiters |
| `retry` | none | `{ maxAttempts?, backoffMs? }` per-job retry policy |

`enqueue(data, options?)` accepts `{ signal, deadline, timeoutMs }`. The
returned promise resolves with the handler's result, so a route can also wait
for a job inline when it makes sense.

The compiler also recognizes typed-handler `WorkQueue<"name">` parameters.
When a route declares one and the app has not registered the token explicitly,
generated code materializes `WorkQueue.create("<name>")` through request-scope
injection. Register the queue explicitly when you need custom options.

## Worker pools

`WorkerPool.create(name, options?)` runs the supplied function across a
bounded set of worker isolates. Use it for CPU-bound or untrusted work where
you want isolation from the HTTP isolate:

```ts
import { WorkerPool } from "sloppy/workers";

const pool = WorkerPool.create("transcode", { workers: 4, maxQueued: 128 });

const result = await pool.run(async (ctx) => {
    // ctx.input, ctx.signal, ctx.deadline
    return transcode(ctx.input);
}, { input: { path: "video.mp4" } });
```

`pool.run` requires the `__sloppy.workers` runtime bridge. Without it the call
rejects with `SLOPPY_E_WORKER_BRIDGE_UNAVAILABLE`. Worker-isolate crashes
surface as `SLOPPY_E_WORKER_CRASHED`.

## Worker isolates

For an explicit worker module, `Worker.start(modulePath, options?)` starts a
single JS isolate:

```ts
import { Worker } from "sloppy/workers";

const worker = await Worker.start("./workers/encoder.js", { memoryLimitMb: 256 });
const reply = await worker.invoke("encode", { input: bytes });
await worker.stop();
```

`Worker` is the Sloppy worker isolate handle, **not** the Web `Worker` /
`MessagePort` shape. Payloads are copied across the boundary (no transfer, no
`SharedArrayBuffer`, no `MessageChannel`).

## Cancellation

`WorkerCancellationController` is the producer side; the attached signal is
the consumer side. Worker APIs that accept `signal` also accept an
`AbortSignal` or a `CancellationSignal` from `sloppy/time`, so the same
deadline can flow through HTTP, providers, and worker handlers:

```ts
import { WorkerCancellationController } from "sloppy/workers";

const ctl = new WorkerCancellationController();
setTimeout(() => ctl.cancel("user-cancelled"), 1000);
await doWork(ctl.signal);
```

## Lifecycle and shutdown

- `app.use(resource)` starts background services and queues immediately.
- App shutdown does not yet auto-stop workers — call `stop(...)` yourself.
- `queue.stop({ drain: true })` (default) lets active jobs finish and rejects
  new enqueues with `SLOPPY_E_WORK_QUEUE_STOPPED`.
- `queue.stop({ drain: false })` rejects every queued and backpressure-waiting
  job with `SLOPPY_E_WORKER_SHUTDOWN_CANCELLED`.

## Errors

Workers throw `SloppyWorkerError` with stable codes. Common ones:

| Code | Meaning |
| --- | --- |
| `SLOPPY_E_WORK_JOB_CANCELLED` | A job was cancelled |
| `SLOPPY_E_WORK_JOB_TIMEOUT` | A job exceeded its deadline |
| `SLOPPY_E_WORK_QUEUE_FULL` | A bounded queue rejected an enqueue |
| `SLOPPY_E_WORK_QUEUE_STOPPED` | The queue is stopped |
| `SLOPPY_E_WORK_RETRY_EXHAUSTED` | Retries exhausted for a job |
| `SLOPPY_E_WORKER_SHUTDOWN_CANCELLED` | Cancelled because the worker host is shutting down |
| `SLOPPY_E_WORKER_BRIDGE_UNAVAILABLE` | The native worker bridge isn't available |
| `SLOPPY_E_WORKER_CRASHED` | The worker isolate crashed |
| `SLOPPY_E_BACKGROUND_SERVICE_FAILED` | A background service handler threw |

The full code list lives in `stdlib/sloppy/workers.js`.

## Examples

- `examples/workers-background-service` — long-running service alongside HTTP
- `examples/workers-workqueue` — producer/consumer queue with retry
- `examples/workers-workerpool` — bounded pool of worker isolates
- `examples/workers-js-isolate` — single `Worker.start` isolate
- `examples/workers-shutdown` — drain-vs-cancel `stop()` behavior
- `examples/core-worker-time` — workers with cancellation and deadlines

See [Workers](../api/workers.md) for the full API surface and
[Reference / Workers](../reference/workers.md) for the matrix. Current support
boundaries are tracked in the [stability matrix](../reference/stability.md).
