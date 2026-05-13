# Jobs

`sloppy/jobs` is Sloppy's durable background-job and scheduler API. It stores
job state in a Sloppy data provider instead of in process memory, so workers can
claim work, record attempts, retry failures, run recurring schedules, and expose
operator metadata through the jobs admin backend.

Use `sloppy/time` for in-process intervals. Use `sloppy/jobs` when work needs a
database-backed queue, durable retries, worker leases, recurring cron schedules,
or distributed locks.

```ts
import { Jobs } from "sloppy/jobs";
import { data, schema } from "sloppy";

const db = data.sqlite.open({ path: "./app.db", access: "readwrite" });
const jobs = Jobs.create({ storage: Jobs.storage.sqlite(db) });

await jobs.storage.init();

const EmailPayload = schema.object({
    to: schema.string().email(),
    token: schema.string().min(1),
});

jobs.define("send-email", {
    input: EmailPayload,
    queue: "emails",
    retries: {
        maxAttempts: 5,
        backoff: "exponential",
        initialDelayMs: 1000,
        maxDelayMs: 60000,
        jitter: true,
    },
    timeoutMs: 30000,
    payloadRedactionKeys: ["token"],
}, async (ctx, input) => {
    await sendEmail(input, { signal: ctx.signal });
});

await jobs.enqueue("send-email", {
    to: "ada@example.com",
    token: "secret",
}, {
    idempotencyKey: "welcome:ada",
});
```

## Storage

Create storage from a Sloppy data provider connection:

```ts
Jobs.storage.sqlite(db);
Jobs.storage.postgres(db);
Jobs.storage.sqlserver(db);
Jobs.storage.from(db);
```

`storage.init()` creates the scheduler schema and records its schema version.
It is idempotent. A schema mismatch reports
`SLOPPY_E_JOBS_SCHEMA_VERSION_MISMATCH`.

Scheduler-generated IDs use Sloppy random entropy and readable prefixes such as
`job_`, `attempt_`, `event_`, `worker_`, and `recurring_`. They do not derive
uniqueness from `Date.now()` or a process-local counter.

Storage timestamps use the provider database clock by default. SQLite uses the
SQLite clock, PostgreSQL uses `clock_timestamp()`, and SQL Server uses
`sysutcdatetime()`. Delayed enqueue and recurring registration derive run times
from that database clock unless the caller supplies an explicit `runAt`.

## Job Definitions

Jobs are named. The name is the durable identity stored in scheduler tables.

```ts
jobs.define("sync-users", {
    queue: "sync",
    input: schema.object({ tenantId: schema.string().min(1) }),
    retries: { maxAttempts: 3, backoff: "fixed", initialDelayMs: 5000 },
    timeoutMs: 120000,
}, async (ctx, input) => {
    await syncUsers(input.tenantId, { signal: ctx.signal });
});
```

Definitions support:

| Option | Meaning |
| --- | --- |
| `input` | Optional schema with `validate(value)` used at enqueue and execution |
| `queue` | Default queue for this job definition |
| `retries` / `retry` | Retry policy: `maxAttempts`, `backoff`, `initialDelayMs`, `maxDelayMs`, `jitter` |
| `timeoutMs` | Cooperative handler timeout; timed-out jobs receive an aborted `ctx.signal` |
| `payloadRedactionKeys` | Extra payload keys redacted from admin views |
| `metadata` | Definition metadata for tools and admin backends |

Duplicate definitions throw `SLOPPY_E_JOBS_DUPLICATE_JOB`.

## Enqueue

```ts
await jobs.enqueue("send-email", payload);
await jobs.enqueueDelayed("send-email", payload, 60000);
await jobs.enqueueAt("send-email", payload, "2026-05-12T18:00:00.000Z");
```

`enqueue` options:

| Option | Meaning |
| --- | --- |
| `queue` | Queue override |
| `priority` | Higher priority claims first inside a queue |
| `delayMs` | Store as scheduled until due |
| `runAt` | ISO timestamp or `Date`; not claimable before this time |
| `idempotencyKey` | Unique key; duplicate enqueue returns the existing job |
| `maxAttempts` | Per-job retry cap |
| `retries` / `retry` | Per-job retry policy |
| `timeoutMs` | Per-job timeout override |
| `correlationId` | Diagnostic correlation id |
| `metadata` | Safe operational metadata |

Unknown job names fail before storage with `SLOPPY_E_JOBS_UNKNOWN_JOB`. Invalid
payloads fail with `SLOPPY_E_JOBS_INVALID_PAYLOAD`.

## Workers

```ts
const worker = jobs.createWorker({
    id: "worker-1",
    queues: ["default", "emails"],
    concurrency: 4,
    leaseMs: 30000,
    pollIntervalMs: 500,
    idleBackoffMs: 5000,
    heartbeatIntervalMs: 5000,
    shutdownTimeoutMs: 30000,
});

await worker.start();
// ...
await worker.stop("shutdown");
```

Workers register in durable storage, heartbeat while active, claim due jobs
using provider-specific locking, and create attempt/history rows. A graceful
stop stops claiming new jobs and waits for in-flight jobs until the shutdown
timeout.

Timeouts are cooperative. The worker aborts `ctx.signal` and records
`SLOPPY_E_JOBS_TIMEOUT`; handlers should observe the signal and stop side
effects promptly. A handler that completes after timeout does not complete the
job a second time.

For deterministic tests and one-shot commands, use `worker.runOnce()`.

## Recurring Jobs

```ts
await jobs.recurring("sync-users-every-five", "sync-users", {
    tenantId: "main",
}, {
    cron: "*/5 * * * *",
    timezone: "UTC",
    queue: "sync",
    misfirePolicy: "run-once",
});

await jobs.tickRecurring({ owner: "scheduler-1" });
```

Recurring schedules are stored in `sloppy_recurring_jobs`. `tickRecurring` uses
the scheduler lock service, reads due schedules with a bounded query, enqueues
one job per due occurrence using a recurrence idempotency key, and updates
`lastRunAt` and `nextRunAt`.

Cron expressions use five fields: minute, hour, day of month, month, day of
week. UTC is supported.

Misfire policies:

| Policy | Behavior |
| --- | --- |
| `ignore` | Advance the schedule without enqueueing for the missed occurrence |
| `run-once` | Enqueue one job for the due occurrence |
| `catch-up-limited` | Enqueue bounded missed occurrences using per-occurrence idempotency keys |

## Locks

```ts
await jobs.locks("owner-1").with("nightly-report", { ttlMs: 30000 }, async () => {
    await runNightlyReport();
});
```

Locks use `sloppy_job_locks`. They support acquire, release, extend, expiry
recovery, and conflict diagnostics. Release or extend by a different owner
throws `SLOPPY_E_JOBS_LOCK_CONFLICT`.

## Admin Backend

```ts
const admin = jobs.admin();

await admin.overview();
await admin.listJobs({ status: "dead", pageSize: 50 });
await admin.getJob(jobId);
await admin.attempts(jobId);
await admin.events(jobId);
await admin.retry(jobId);
await admin.cancel(jobId);
await admin.delete(jobId);
await admin.listRecurring();
await admin.pauseRecurring("sync-users-every-five");
await admin.resumeRecurring("sync-users-every-five");
await admin.triggerRecurring("sync-users-every-five");
await admin.listWorkers();
await admin.listLocks();
await admin.cleanup({ keepSucceededMs: 86400000, batchSize: 100 });
```

Admin payload views use redacted previews by default. Raw payload access is not
part of the admin service.

## State Machine

Valid states are `scheduled`, `queued`, `processing`, `succeeded`, `failed`,
`retrying`, `dead`, `cancelled`, and `deleted`.

Every transition is validated and writes an event row. Invalid transitions
throw `SLOPPY_E_JOBS_TRANSITION_INVALID`.

## Diagnostics

Common error codes:

| Code | Meaning |
| --- | --- |
| `SLOPPY_E_JOBS_UNKNOWN_JOB` | Missing job definition or job row |
| `SLOPPY_E_JOBS_INVALID_PAYLOAD` | Payload failed JSON or schema validation |
| `SLOPPY_E_JOBS_SCHEMA_VERSION_MISMATCH` | Stored schema version is incompatible |
| `SLOPPY_E_JOBS_TRANSITION_INVALID` | State transition is not allowed |
| `SLOPPY_E_JOBS_LOCK_CONFLICT` | Lock is held by another owner |
| `SLOPPY_E_JOBS_HANDLER_FAILED` | Handler threw or rejected |
| `SLOPPY_E_JOBS_TIMEOUT` | Handler exceeded `timeoutMs` |
| `SLOPPY_E_JOBS_RECURRING_INVALID_CRON` | Cron expression or timezone is invalid |

Payload redaction covers keys containing `password`, `token`, `secret`,
`authorization`, `cookie`, or `key`, plus definition-specific redaction keys.

## CLI And Validation

The native `sloppy jobs` command operates SQLite scheduler tables. PostgreSQL
and SQL Server scheduler validation runs through Sloppy Program Mode live lanes
that use the same public Jobs API with those providers.

See [sloppy jobs](../cli/jobs.md), [Jobs storage](../reference/jobs-storage.md),
and [Scheduler internals](../internals/scheduler.md).
