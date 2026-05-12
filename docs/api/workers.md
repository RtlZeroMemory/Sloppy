# Workers

Workers cover background work that runs alongside HTTP request handling:
long-running services, cancellable operations, queued jobs, and isolated
worker scripts.

The public exports are available from both `"sloppy"` and `"sloppy/workers"`.
Compiler source input should import from `"sloppy/workers"` so the Plan
records the worker runtime feature; app-host JS code may import from
`"sloppy"` for convenience.

```ts
import {
    BackgroundService,
    WorkQueue,
    WorkerPool,
    Worker,
    WorkerCancellationController,
    WorkerCancellationSignal,
    SloppyWorkerError,
} from "sloppy/workers";
```

The constructible classes use static `.create(...)` factories
(`Worker.start(...)` for worker isolates) rather than `new`.
`WorkerCancellationController` is the one that uses `new`.
Factory/start option bags must be plain objects when provided.

## Current status

`BackgroundService`, `WorkQueue`, and the cancellation primitives are
implemented in pure JS in the bootstrap stdlib and run on the app-host.
`WorkerPool.run` and `Worker.start` need the `__sloppy.workers` runtime
bridge; without it the call rejects with `SLOPPY_E_WORKER_BRIDGE_UNAVAILABLE`.

## Cancellation

A `WorkerCancellationController` is the producer side. The signal attached
to it is the consumer side.

```ts
import { WorkerCancellationController } from "sloppy/workers";

const ctl = new WorkerCancellationController();

setTimeout(() => ctl.cancel("user-cancelled"), 1000);

await doWork(ctl.signal);
```

A signal exposes:

| Member                                 | Purpose                                           |
| -------------------------------------- | ------------------------------------------------- |
| `signal.cancelled`                     | `true` once cancelled                             |
| `signal.aborted`                       | mirror of `cancelled`, matching `AbortSignal`     |
| `signal.reason`                        | the reason passed to `cancel(reason)`             |
| `signal.throwIfCancelled()`            | throws `SloppyWorkerError` if already cancelled   |
| `signal.addEventListener("abort", fn)` | run `fn` when cancelled                           |
| `signal.removeEventListener(...)`      | detach a listener                                 |

The shape is compatible with `AbortSignal` consumers in practice, but it is
`WorkerCancellationSignal`, not a Web `AbortSignal`. Where the worker APIs
accept a `signal`, they also accept either an `AbortSignal` or a
`CancellationSignal` from `sloppy/time`.

## Errors

Worker code throws `SloppyWorkerError` for runtime conditions:

```ts
import { SloppyWorkerError } from "sloppy/workers";

try {
    await queue.enqueue(payload);
} catch (err) {
    if (err instanceof SloppyWorkerError &&
        err.code === "SLOPPY_E_WORK_QUEUE_FULL") {
        // back off
    } else {
        throw err;
    }
}
```

Codes used today (full list in `stdlib/sloppy/workers.js`):

| Code                                          | Meaning                                                 |
| --------------------------------------------- | ------------------------------------------------------- |
| `SLOPPY_E_WORK_JOB_CANCELLED`                 | A job was cancelled                                     |
| `SLOPPY_E_WORK_JOB_TIMEOUT`                   | A job exceeded its deadline                             |
| `SLOPPY_E_WORK_QUEUE_FULL`                    | A bounded queue rejected an enqueue                     |
| `SLOPPY_E_WORK_QUEUE_STOPPED`                 | The queue is stopped                                    |
| `SLOPPY_E_WORK_RETRY_EXHAUSTED`               | Retries exhausted for a job                             |
| `SLOPPY_E_WORKER_SHUTDOWN_CANCELLED`          | Cancelled because the worker host is shutting down      |
| `SLOPPY_E_WORKER_STALE_HANDLE`                | The handle was used after stop/dispose                  |
| `SLOPPY_E_WORKER_BRIDGE_UNAVAILABLE`          | The native worker bridge isn't available                |
| `SLOPPY_E_WORKER_CRASHED`                     | The worker isolate crashed                              |
| `SLOPPY_E_WORKER_ISOLATE_STARTUP_FAILED`      | Worker isolate failed to start                          |
| `SLOPPY_E_WORKER_UNSUPPORTED_PAYLOAD`         | Message payload not supported                           |
| `SLOPPY_E_WORKER_MESSAGE_SERIALIZATION_FAILED`| Message could not be serialized across the boundary     |
| `SLOPPY_E_BACKGROUND_SERVICE_FAILED`          | A background service handler threw                      |

## Background services

A `BackgroundService` is a long-running task with start/stop semantics.
Construct it with the static factory:

```ts
import { Sloppy } from "sloppy";
import { BackgroundService } from "sloppy/workers";

const tick = BackgroundService.create("clock-tick", async (svc) => {
    while (!svc.signal.cancelled) {
        await sleep(1000, svc.signal);
        // svc.name === "clock-tick"
    }
});

const app = Sloppy.create();
app.use(tick);
```

The handler receives a context object — `{ name, signal }` — not just the
signal. `app.use(...)` recognizes worker resources and calls their
`__sloppyStartForApp(app)` hook **immediately at registration**, so `tick`
is already running once `app.use(tick)` returns.

Handle methods and state:

| Member | Purpose |
| --- | --- |
| `service.start()` | start (idempotent until completion) |
| `service.stop(reason?)` | request stop; resolves once handler completes |
| `service.state` | `"created" \| "running" \| "stopped" \| "completed" \| "failed"` |
| `service.failure` | `SloppyWorkerError` set when state is `"failed"` |

Shutdown integration is still being hardened — the app does not
automatically call worker `__sloppyStopForApp` hooks today. If you need a
clean stop, hold a reference to the resource and call `tick.stop(...)`
yourself (e.g. from a process-level signal handler).

## Work queues

`WorkQueue.create("name", options?)` returns a named queue with explicit
producer and consumer sides.

```ts
import { Sloppy, Results } from "sloppy";
import { WorkQueue } from "sloppy/workers";
import { Deadline } from "sloppy/time";

const emails = WorkQueue.create("emails", {
    concurrency: 4,
    maxQueued: 256,
});

emails.process(async (job, ctx) => {
    // job.id, job.data, job.attempt
    // ctx.signal, ctx.deadline, ctx.attempt
    await sendEmail(job.data, ctx.signal);
});

const app = Sloppy.create();
app.post("/send", (request) => {
    const body = request.json();
    return emails.enqueue(body, { deadline: Deadline.after(5000) })
        .then(() => Results.accepted({ queued: true }));
});
```

`WorkQueue.create` options:

| Option | Default | Notes |
| --- | --- | --- |
| `maxQueued` | `1024` | bound on queued work |
| `concurrency` | `1` | active jobs at once |
| `overflow` | `"reject"` | `"reject" \| "backpressure"` when full |
| `maxBackpressureWaiters` | `maxQueued` | cap on `backpressure` waiters |
| `retry` | none | `{ attempts?, backoff? }` per-job retry policy |

WorkQueue `overflow: "backpressure"` is queue admission backpressure for
promises waiting to enqueue. It is separate from the native Core stream
foundation and does not expose stream chunks or writable handles.

`enqueue(data, options?)` accepts `{ signal, deadline, timeoutMs }`. The
returned promise resolves with the handler's result.

Handle methods and state:

| Member | Purpose |
| --- | --- |
| `queue.process(handler)` | register the consumer (`async (job, ctx) => result`) |
| `queue.enqueue(data, options?)` | enqueue; resolves to handler result |
| `queue.drain()` | resolve when every active and queued job finishes |
| `queue.stop({ drain? })` | stop accepting; `drain: true` (default) waits for active jobs |
| `queue.state` | frozen snapshot: `{ queued, active, stats: { enqueued, completed, failed, cancelled, timedOut, retryExhausted, overflow } }` |

`stop({ drain: false })` rejects every queued and backpressure-waiting job
with `SLOPPY_E_WORKER_SHUTDOWN_CANCELLED`. `stop({ drain: true })` lets
active jobs finish but rejects new enqueues with
`SLOPPY_E_WORK_QUEUE_STOPPED`.

Compiler-inferred `WorkQueue<"name">` typed handler parameters are implemented
for the current typed-handler fixture subset. The compiler emits an inferred
`queue.<name>` capability and generated code materializes
`WorkQueue.create("<name>")` through request-scope injection when the app has
not registered that token explicitly. Register queues explicitly when you need
custom queue options.

## Worker pools

`WorkerPool.create("name", options?)` runs work across a bounded set of
worker isolates. The pool accepts a per-call function and forwards it to a
worker.

```ts
import { WorkerPool } from "sloppy/workers";

const pool = WorkerPool.create("transcode", {
    workers: 4,
    maxQueued: 128,
});

const result = await pool.run(async (ctx) => {
    // ctx.input, ctx.signal, ctx.deadline
    return transcode(ctx.input);
}, { input: { path: "video.mp4" } });
```

Options:

| Option | Default | Notes |
| --- | --- | --- |
| `workers` | `1` | bound on concurrent isolates |
| `maxQueued` | `64` | bound on queued runs |
| `overflow` | `"reject"` | matches `WorkQueue` semantics |

Handle methods:

| Member | Purpose |
| --- | --- |
| `pool.run(fn, options?)` | dispatch a run; `options` accepts `input`, `signal`, `deadline`, `timeoutMs` |
| `pool.drain()` | resolve when active and queued runs finish |
| `pool.stop({ drain? })` | stop accepting new runs |
| `pool.state` | frozen snapshot of queue state |

`pool.run` requires the `__sloppy.workers` runtime bridge. Without it the
call rejects with `SLOPPY_E_WORKER_BRIDGE_UNAVAILABLE`. Worker-isolate
crashes surface as `SLOPPY_E_WORKER_CRASHED`.

## Worker isolates

`Worker.start(modulePath, options?)` starts a single JS worker isolate.

```ts
import { Worker } from "sloppy/workers";

const worker = await Worker.start("./workers/encoder.js", { memoryLimitMb: 256 });

const result = await worker.invoke("encode", { input: bytes });
const reply = await worker.post({ kind: "ping" });

const off = worker.onMessage((message) => {
    // handle message from worker
});

// ...
off();
await worker.stop();
```

| Method | Purpose |
| --- | --- |
| `worker.invoke(exportName, payload?, options?)` | call a named export in the worker; `options` accepts `signal`, `deadline`, `timeoutMs` |
| `worker.post(message, options?)` | dispatch to the worker's `onMessage` export |
| `worker.onMessage(callback)` | subscribe to worker-emitted messages; returns an unsubscribe function |
| `worker.stop()` | terminate the isolate |
| `worker.modulePath` | the module path passed at start |

Options:

| Option | Default | Notes |
| --- | --- | --- |
| `memoryLimitMb` | `128` | hard memory cap for the isolate |

`Worker.start` requires the `__sloppy.workers` runtime bridge. Without it
the call rejects with `SLOPPY_E_WORKER_BRIDGE_UNAVAILABLE`. Payloads must
be serializable across the boundary — circular references and unsupported
types fail with `SLOPPY_E_WORKER_UNSUPPORTED_PAYLOAD` or
`SLOPPY_E_WORKER_MESSAGE_SERIALIZATION_FAILED`.

## Examples

- `examples/workers-background-service` — long-running service alongside
  HTTP
- `examples/workers-workqueue` — producer/consumer queue with retry
- `examples/workers-workerpool` — bounded pool of worker isolates
- `examples/workers-js-isolate` — single `Worker.start` isolate
- `examples/workers-shutdown` — drain-vs-cancel `stop()` behavior
- `examples/core-worker-time` — workers with cancellation and deadlines

## Boundaries

- No Node `worker_threads` compatibility. `Worker` is not the Web
  `Worker`/`MessagePort` shape; it's the Sloppy worker isolate handle.
- No shared memory, no `SharedArrayBuffer` channels, no `MessageChannel`.
- No native stream handle transfer between workers.
- Job payloads are copied across the worker boundary, not transferred.
- Background services do not yet auto-stop when the app shuts down. Call
  `service.stop(...)` from your shutdown path.

## Compiler source-input support

The compiler accepts `import ... from "sloppy/workers"` and emits the
`stdlib.workers` required feature into the Plan. Aliased and default
imports are rejected. Resources registered through `app.use(...)` carry
Plan metadata (kind, name, queue/pool sizing, worker module path) so
artifacts and `sloppy capabilities` can report them.

## Runtime requirements

`BackgroundService`, `WorkQueue`, and the cancellation primitives are pure
JS — they run anywhere the bootstrap stdlib runs. `WorkerPool.run` and
`Worker.start` require the `__sloppy.workers` V8 intrinsic namespace,
provided by the native runtime when the Plan declares `stdlib.workers`.
