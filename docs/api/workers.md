# Workers

Workers cover background work that runs alongside HTTP request handling:
long-running services, cancellable operations, and queued jobs.

> Experimental. The shapes documented here are real; some surfaces are
> still landing. Treat the section as a tour, not a contract.

The public exports come from `"sloppy"`:

```ts
import {
    BackgroundService,
    WorkQueue,
    WorkerPool,
    Worker,
    WorkerCancellationController,
    WorkerCancellationSignal,
    SloppyWorkerError,
} from "sloppy";
```

The constructible classes use static `.create(...)` factories rather
than `new`. `WorkerCancellationController` is the one that uses `new`.

## Cancellation

A `WorkerCancellationController` is the producer side. The signal
attached to it is the consumer side.

```ts
import { WorkerCancellationController } from "sloppy";

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

The shape is compatible with `AbortSignal` consumers in practice, but
it is `WorkerCancellationSignal`, not a Web `AbortSignal`.

## Errors

Worker code throws `SloppyWorkerError` for runtime conditions:

```ts
import { SloppyWorkerError } from "sloppy";

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
import { Sloppy, BackgroundService } from "sloppy";

const tick = BackgroundService.create("clock-tick", async (svc) => {
    while (!svc.signal.cancelled) {
        await sleep(1000, svc.signal);
        // svc.name === "clock-tick"
    }
});

const app = Sloppy.create();
app.use(tick);
```

The handler receives a context object — `{ name, signal }` — not just
the signal. `app.use(...)` recognizes worker resources and calls their
`__sloppyStartForApp(app)` hook **immediately at registration**, so
`tick` is already running once `app.use(tick)` returns.

Shutdown integration is still being hardened — the app does not
automatically call worker `__sloppyStopForApp` hooks today. If you
need a clean stop, hold a reference to the resource and call
`tick.stop(...)` yourself (e.g. from a process-level signal handler).

## Work queues

`WorkQueue.create("name", options?)` returns a named queue. The
producer side exposes `enqueue(payload, options?)`:

```ts
import { Sloppy, Results, WorkQueue } from "sloppy";

const emails = WorkQueue.create("emails");

const app = Sloppy.create();

app.post("/send", (ctx) => {
    const body = ctx.request.json();
    return emails.enqueue(body, { deadline: Date.now() + 5000 })
        .then(() => Results.accepted({ queued: true }));
});
```

Compiler-inferred `WorkQueue<"name">` typed handler parameters are
upcoming framework work. Until that's exercised by fixtures, register
the queue explicitly as above.

## Worker pools and workers

`WorkerPool.create("name", options?)` runs work across a bounded set
of worker isolates. `Worker.start(modulePath, options?)` starts a
single worker isolate. Both surfaces are small today; for a vetted
pattern see `examples/workers-workerpool/`.
