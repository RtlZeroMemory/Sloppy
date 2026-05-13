# Background tasks and scheduling

This page covers the way Sloppy apps run work alongside HTTP request handling:
long-running services, queued jobs, periodic schedules, and worker isolates.
The in-process primitives live in `sloppy/workers` and `sloppy/time`. Durable
jobs and recurring schedules live in `sloppy/jobs`.

Pick the primitive that matches the shape of the work:

| Primitive | When to use |
| --- | --- |
| `BackgroundService` | App-lifetime tasks (cleanup loops, drains, manual schedules) |
| `Time.every(intervalMs, handler)` | Periodic scheduled task with `pause`/`resume`/`stop`, overlap protection, and `FakeClock` testability |
| `Time.interval(intervalMs)` | `for await` ticks at a fixed cadence, with `stop()` and an optional max tick count |
| `WorkQueue` | Bounded queued jobs with concurrency, retry, and backpressure |
| `Jobs.create({ storage })` | Provider-backed durable jobs, retries, recurring cron schedules, worker leases, and admin metadata |
| `WorkerPool` | CPU-bound or untrusted work offloaded to a bounded set of worker isolates |
| `Worker.start(...)` | A single explicit worker isolate from a module path |

Use `Time.every` for lightweight interval work inside one process. Use
`sloppy/jobs` when work must survive restarts, be claimed by multiple workers,
or run from a recurring cron schedule.

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

## Scheduled tasks

`Time.every(intervalMs, handler, options?)` from `sloppy/time` runs a handler
on a periodic in-process schedule. The schedule is an interval, not a cron
expression, and it is not durable storage. For wall-clock or calendar schedules
that should survive process restarts, use the durable Jobs scheduler.

```ts
import { Sloppy, Results } from "sloppy";
import { Time } from "sloppy/time";

const app = Sloppy.create();

const job = Time.every(60_000, async (ctx) => {
    // ctx.signal, ctx.run, ctx.skippedRuns, ctx.startedAt, ctx.scheduledAt
    await rotateAuditFiles(ctx.signal);
}, {
    immediate: false,   // skip an initial run at t=0
    noOverlap: true,    // skip ticks while the previous run is still active
});

app.mapPost("/audit/pause",  () => { job.pause();  return Results.noContent(); });
app.mapPost("/audit/resume", () => { job.resume(); return Results.noContent(); });
```

Handle methods and state:

| Member | Purpose |
| --- | --- |
| `job.pause()` | pause scheduling without stopping the job |
| `job.resume()` | resume after `pause()` |
| `job.stop(reason?)` | cancel the job and resolve the internal loop |
| `job.running` | `true` while a handler invocation is in flight |
| `job.stopped` | `true` after `stop()` or after `maxRuns` is reached |
| `job.skippedRuns` | count of ticks skipped because a previous run was still active |
| `job.lastError` | the last error a handler threw (handlers are isolated; one failure does not stop the schedule) |
| `job.nextRun` | a `Date` for the next scheduled invocation (or `null` after stop) |

Options accepted by `Time.every`:

| Option | Default | Notes |
| --- | --- | --- |
| `immediate` | `false` | run a tick at `t=0` instead of waiting one interval |
| `noOverlap` | `true` | skip a tick if the previous handler is still running |
| `maxRuns` | unbounded | stop the schedule after this many successful starts |
| `missedRunPolicy` | `"skip"` | only `"skip"` is supported in this alpha |
| `signal` | none | external cancellation signal |
| `clock` | system clock | inject `Time.fakeClock(...)` for deterministic tests |

For a pull-style schedule (you drive each tick yourself), use
`Time.interval(intervalMs, options?)` — it is an async iterable that yields
`{ index, at, scheduledAt }` ticks and exposes `stop()`:

```ts
const ticker = Time.interval(1000, { maxTicks: 60 });
for await (const tick of ticker) {
    // tick.index is 1-based; tick.at is a Date
    if (shouldStop()) {
        ticker.stop();
    }
}
```

### Test schedules deterministically

`Time.fakeClock(...)` advances time on demand, so schedules can be tested
without sleeping in real time:

```ts
import { Time } from "sloppy/time";

const clock = Time.fakeClock({ now: new Date("2026-01-01T00:00:00Z") });
let runs = 0;

const job = Time.every(60_000, () => { runs += 1; }, { clock, immediate: true });

await clock.advanceMs(60_000);
// runs === 2 (one immediate, one after the first interval)
await job.stop();
```

## Durable jobs and recurring schedules

`sloppy/jobs` stores job state in a Sloppy data provider. It is the right tool
for email delivery, webhook delivery, synchronization, cleanup, reports, and
other work that needs retries, leases, recurring schedules, or operator
visibility.

```ts
import { Jobs } from "sloppy/jobs";
import { data, schema } from "sloppy";

const db = data.sqlite.open({ path: "./app.db", access: "readwrite" });
const jobs = Jobs.create({ storage: Jobs.storage.sqlite(db) });

await jobs.storage.init();

jobs.define("send-email", {
    queue: "emails",
    input: schema.object({ to: schema.string().email() }),
    retries: { maxAttempts: 5, backoff: "exponential", initialDelayMs: 1000 },
    timeoutMs: 30000,
}, async (ctx, input) => {
    await sendEmail(input, { signal: ctx.signal });
});

await jobs.enqueue("send-email", { to: "ada@example.test" }, {
    idempotencyKey: "welcome:ada",
});

await jobs.recurring("nightly-cleanup", "cleanup", {}, {
    cron: "0 2 * * *",
    timezone: "UTC",
    misfirePolicy: "run-once",
});
```

Recurring jobs use five-field UTC cron expressions. Workers register and
heartbeat in durable storage, claim due jobs with provider-specific locking,
and record attempts and events. See [Jobs](../api/jobs.md) for the API and
[sloppy jobs](../cli/jobs.md) for SQLite scheduler administration.

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
- `examples/jobs-basic` — SQLite durable jobs, idempotency, redaction, worker run-once
- `examples/jobs-recurring` — recurring cron schedules and manual ticks
- `examples/jobs-concurrency-*` — provider-backed claim and lease behavior

See [Workers](../api/workers.md) for the full API surface and
[Jobs](../api/jobs.md) for durable scheduling. Current support boundaries are
tracked in the [stability matrix](../reference/stability.md).
