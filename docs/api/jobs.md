# Jobs

`sloppy/jobs` is the durable scheduler API for background jobs stored in a
database. It covers fire-and-forget jobs, delayed jobs, recurring jobs, worker
coordination, retries, dead-letter handling, idempotency, distributed locks,
and backend administration.

```ts
import { Jobs } from "sloppy/jobs";
import { data, Schema } from "sloppy";

const db = data.sqlite.open({ path: "./app.db", access: "readwrite" });
const jobs = Jobs.create({ storage: Jobs.storage.sqlite(db) });

await jobs.storage.init();

const EmailPayload = Schema.object({
    to: Schema.string().email(),
    token: Schema.string().min(1),
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

The bootstrap package re-exports jobs from both `"sloppy"` and `"sloppy/jobs"`.
Compiler source input should import runtime jobs APIs from `"sloppy/jobs"` so
the Plan records the `stdlib.jobs` required feature and Program Mode binds the
jobs stdlib module.

## Storage

Create storage from a Sloppy data provider connection:

```ts
Jobs.storage.sqlite(db);
Jobs.storage.postgres(db);
Jobs.storage.sqlserver(db);
Jobs.storage.from(db);
```

`storage.init()` creates the scheduler schema and records the schema version.
It is idempotent. A schema version mismatch throws
`SLOPPY_E_JOBS_SCHEMA_VERSION_MISMATCH`.

## Job Definitions

Jobs are named. The name is the durable identity stored in the database, so do
not use closures or request-local values as job identity.

```ts
jobs.define("sync-users", {
    queue: "sync",
    input: Schema.object({ tenantId: Schema.string().min(1) }),
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
| `timeoutMs` | Cooperative handler timeout; timed-out jobs receive an aborted `ctx.signal` and fail with `SLOPPY_E_JOBS_TIMEOUT` |
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
| `runAt` | ISO time or `Date`; not claimable before this time |
| `idempotencyKey` | Unique key; duplicate enqueue returns the existing job |
| `maxAttempts` | Per-job retry cap |
| `retries` / `retry` | Per-job retry policy |
| `timeoutMs` | Per-job timeout override |
| `correlationId` | Diagnostic correlation id |
| `metadata` | Safe operational metadata |

Unknown job names fail before storage with `SLOPPY_E_JOBS_UNKNOWN_JOB`.
Invalid payloads fail with `SLOPPY_E_JOBS_INVALID_PAYLOAD`.

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

Workers register in `sloppy_job_workers`, heartbeat while active, claim due
jobs using provider-specific locking, and create attempt/history rows. A
graceful stop stops claiming new jobs, waits for in-flight jobs until the
shutdown timeout, then marks the worker stopped.

Timeouts are cooperative. The worker aborts `ctx.signal` and records
`SLOPPY_E_JOBS_TIMEOUT`; handlers should listen to the signal and stop side
effects promptly. A handler that completes after the timeout does not complete
the job a second time.

For deterministic tests and one-shot worker commands, use `worker.runOnce()`.

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

Recurring schedules are stored in `sloppy_recurring_jobs`. `tickRecurring`
uses the scheduler lock service, reads due schedules with a bounded query,
enqueues one job per due occurrence using a recurrence idempotency key, and
updates `lastRunAt` and `nextRunAt`.

Cron expressions use five fields: minute, hour, day of month, month, day of
week. UTC is supported. Invalid cron expressions throw
`SLOPPY_E_JOBS_RECURRING_INVALID_CRON`.

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

## CLI

SQLite scheduler tables can be operated with native admin commands:

```powershell
sloppy jobs init --provider sqlite --database .sloppy/jobs.db
sloppy jobs status --provider sqlite --database .sloppy/jobs.db
sloppy jobs list --provider sqlite --database .sloppy/jobs.db
sloppy jobs show <id> --provider sqlite --database .sloppy/jobs.db
sloppy jobs retry <id> --provider sqlite --database .sloppy/jobs.db
sloppy jobs cancel <id> --provider sqlite --database .sloppy/jobs.db
sloppy jobs delete <id> --provider sqlite --database .sloppy/jobs.db
sloppy jobs workers --provider sqlite --database .sloppy/jobs.db
sloppy jobs recurring list --provider sqlite --database .sloppy/jobs.db
sloppy jobs recurring pause <name> --provider sqlite --database .sloppy/jobs.db
sloppy jobs recurring resume <name> --provider sqlite --database .sloppy/jobs.db
sloppy jobs recurring trigger <name> --provider sqlite --database .sloppy/jobs.db
sloppy jobs locks --provider sqlite --database .sloppy/jobs.db
```

PostgreSQL and SQL Server scheduler validation uses Sloppy Program Mode live
lanes rather than native C admin clients.

## Live Validation

The optional jobs live lanes run Sloppy source through `sloppy run` and use
Sloppy provider APIs:

```powershell
.\tools\windows\test-live-jobs-sqlite.ps1
.\tools\windows\test-live-jobs-postgres.ps1
.\tools\windows\test-live-jobs-sqlserver.ps1
.\tools\windows\test-live-jobs.ps1 -Provider all
```

SQLite uses `examples/jobs-basic/main.ts`, `examples/jobs-recurring/main.ts`,
`examples/jobs-concurrency-sqlite/main.ts`, and process-level claim checks via
`examples/jobs-concurrency-step/main.ts`. PostgreSQL uses
`examples/jobs-postgres-worker/main.ts` and `examples/jobs-concurrency/main.ts`.
SQL Server uses `examples/jobs-sqlserver-worker/main.ts` plus multiple
`sloppy run` processes running `examples/jobs-concurrency-step/main.ts` against
the same live database, so idempotency, lock, claim, and lease races happen in
SQL Server rather than in a Node harness.

The optional benchmark lane also uses Sloppy Program Mode:

```powershell
.\tools\windows\bench-jobs.ps1 -JobCount 1000
```

## State Machine

Valid states:

`scheduled`, `queued`, `processing`, `succeeded`, `failed`, `retrying`,
`dead`, `cancelled`, `deleted`.

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
