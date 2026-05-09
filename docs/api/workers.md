# Workers

Workers cover background work that runs alongside HTTP request handling:
long-running services, cancellable operations, and queued jobs.

> Experimental. The shapes documented here are real; some pool/queue surfaces
> are still landing. Treat the section as a tour, not a contract.

## Cancellation

A `WorkerCancellationController` is the producer side of a cancel signal.
A `WorkerCancellationSignal` is the consumer side.

```ts
import { WorkerCancellationController } from "sloppy";

const ctl = new WorkerCancellationController();

setTimeout(() => ctl.cancel("user-cancelled"), 1000);

await doWork(ctl.signal);
```

A signal exposes:

| Member                   | Purpose                                            |
| ------------------------ | -------------------------------------------------- |
| `signal.cancelled`       | `true` once cancelled                              |
| `signal.aborted`         | mirror of `cancelled`, mirrors `AbortSignal`       |
| `signal.reason`          | the reason passed to `cancel(reason)`              |
| `signal.throwIfCancelled()` | throws if already cancelled                     |
| `signal.addEventListener("abort", fn)` | run `fn` when cancelled              |
| `signal.removeEventListener(...)` | detach a listener                         |

Signals are compatible with `AbortSignal` consumers in practice — the
shape matches.

## Errors

Worker code throws `SloppyWorkerError` when the runtime needs to surface
something specific (cancellation, queue full, etc):

```ts
import { SloppyWorkerError } from "sloppy";

try {
    await queue.enqueue(payload);
} catch (err) {
    if (err instanceof SloppyWorkerError && err.code === "QUEUE_FULL") {
        // back off
    } else {
        throw err;
    }
}
```

Typical codes:

| Code            | Meaning                                |
| --------------- | -------------------------------------- |
| `CANCELLED`     | The operation was cancelled            |
| `DEADLINE`      | A deadline expired                     |
| `QUEUE_FULL`    | A bounded queue rejected the item      |
| `SHUTDOWN`      | The worker host is shutting down       |

## Background services

A `BackgroundService` is a long-running task with start/stop semantics. The
typical use is something that polls or processes work alongside the HTTP
server.

```ts
import { Sloppy, BackgroundService } from "sloppy";

const tick = new BackgroundService("clock-tick", async (signal) => {
    while (!signal.cancelled) {
        await sleep(1000, signal);
        ctx.log.info("tick");
    }
});

const app = Sloppy.create();
app.use(tick);
```

`app.use(...)` recognizes worker resources and starts them when the app
starts; they shut down gracefully on app shutdown.

## Work queues

`WorkQueue.create("name")` returns a named queue. The queue exposes
`enqueue(payload, options?)` and exposes consumer hooks the runtime sets
up for you when the queue is referenced from a typed handler:

```ts
import { Sloppy, Results, WorkQueue } from "sloppy";

const emails = WorkQueue.create("emails");

const app = Sloppy.create();

app.post("/send", async (ctx) => {
    const body = await ctx.request.json();
    await emails.enqueue(body, { deadline: Date.now() + 5000 });
    return Results.accepted({ queued: true });
});
```

The compiler can infer queue capabilities from `WorkQueue<"name">` typed
handler parameters and wire up the default service registration for you.

## Worker pools

`WorkerPool` runs work across a bounded set of worker isolates. The current
public surface is small — most apps don't need it. If you do, look at the
existing `examples/workers-workerpool/` example for a vetted pattern.
