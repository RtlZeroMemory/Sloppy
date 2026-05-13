# Scheduler Internals

The durable scheduler is a bootstrap stdlib runtime backed by Sloppy data
providers. It deliberately stores durable identity in database rows instead of
JavaScript closures.

## Layers

```text
Application code
  Jobs.define / enqueue / recurring / createWorker / admin
      |
stdlib/sloppy/jobs.js
  validation, state machine, retry, locks, worker loop
      |
Sloppy data provider connection
  sqlite / postgres / sqlserver query, exec, transaction
      |
Database tables
  sloppy_jobs, attempts, recurring, workers, locks, events
```

The compiler recognizes imports from `sloppy/jobs` and emits
`requiredFeatures: ["stdlib.jobs"]`, `features.jobs: true`, and
`strongPlan.evidence.jobs: true`.

Job definitions are runtime dynamic. The compiler marks the jobs runtime
feature, but it does not statically extract `Jobs.define(...)` definitions into
the Plan; scheduler definition metadata comes from runtime calls such as
`jobs.__sloppyPlanMetadata()`.

## Storage Adapter

`SchedulerStorage` is provider-neutral at the API boundary and provider-aware
inside SQL generation. It detects the provider from the data connection
`__debug().kind` or from an explicit storage factory.

The adapter owns:

- schema initialization and version checks
- enqueue/idempotency
- listing and bounded filters
- state transitions and history events
- worker registration and heartbeat
- provider-specific claim SQL
- completion/failure/retry/dead-letter updates
- recurring schedule storage and bounded tick reads
- distributed locks
- retention cleanup

All multi-step operations run through `db.transaction(callback)` when the
provider exposes it. Fake/test providers can use the same callback contract.

## Worker Runtime

`SchedulerWorker` is a cooperative worker loop:

1. register worker row
2. heartbeat
3. claim due jobs for configured queues
4. execute up to `concurrency`
5. extend leases when the handler calls `ctx.extendLease()`
6. complete, retry, dead-letter, or timeout the job
7. stop claiming on shutdown and drain in-flight work until timeout

Handlers receive:

| Field | Meaning |
| --- | --- |
| `id` | Job id |
| `name` | Job definition name |
| `queue` | Claimed queue |
| `attempt` | Current attempt number |
| `signal` | Cancellation signal for shutdown/timeout |
| `extendLease(ms?)` | Extend the database lease |

Timeouts use a linked cancellation controller, abort `ctx.signal`, and fail the
job with `SLOPPY_E_JOBS_TIMEOUT`. Cancellation is cooperative: handlers must
observe the signal to stop their own side effects. Late handler completion
after a timeout is ignored by the worker and must not create a second terminal
transition.

## Retry And Dead Letter

Failure records the attempt row, updates the job error fields, and creates a
history event. If `attempt_count >= max_attempts`, the next state is `dead`.
Otherwise the job enters `retrying`, gets `next_retry_at`, and becomes queued
again once due.

Backoff modes:

- `none`
- `fixed`
- `exponential`

`jitter: true` randomizes the delay within the configured capped delay.

## Recurring Scheduler

Recurring schedules are rows in `sloppy_recurring_jobs`. A tick uses the
distributed lock named `sloppy.jobs.recurring.tick` to prevent duplicate
enqueue work across app instances. Each occurrence uses a deterministic
idempotency key based on schedule name and occurrence timestamp.

The tick is bounded by a page size and reads due schedules in next-run order.

## Admin Backend

The admin service is intentionally a backend object, not a public HTTP
surface. It exposes the operations needed by CLI tooling and operator
interfaces:

- overview counts
- jobs list/detail
- attempts/events
- retry/cancel/delete
- recurring pause/resume/trigger
- workers/locks
- retention cleanup

Payload previews are redacted. The admin service does not expose raw payload
inspection.

The native `sloppy jobs` command currently operates the SQLite scheduler tables
directly. PostgreSQL and SQL Server live validation uses Sloppy Program Mode
admin workloads against those providers. SQL Server race validation is
process-level: the live script starts multiple `sloppy run` Program Mode
processes against the same database so duplicate enqueue, lock, claim, and
lease recovery assertions exercise SQL Server locking instead of one V8 bridge
instance.

## Diagnostics

Scheduler diagnostics use `SloppyJobsError` with stable `code` and frozen
`details`. Details are for job id, job name, queue, worker id, attempt number,
diagnostic id, correlation id, and safe messages.

Diagnostic-producing paths must not include raw payload JSON, connection
strings, database parameters, or secrets.

## Provider Notes

SQLite is suitable for local and single-node durable scheduling. Claiming runs
inside a transaction and uses conditional updates to avoid double settlement.

PostgreSQL uses row locks with `FOR UPDATE SKIP LOCKED` for claim scans.

SQL Server uses `UPDLOCK`, `READPAST`, and `ROWLOCK` on claim scans.

All provider implementations keep reads bounded with explicit page or batch
sizes.
