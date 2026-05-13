# Scheduler Internals

The durable scheduler is a bootstrap stdlib runtime backed by Sloppy data
providers. It stores durable identity in database rows instead of JavaScript
closures.

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

## Compiler Boundary

The compiler recognizes imports from `sloppy/jobs` and emits the jobs stdlib
runtime feature, along with the time and crypto stdlib features used by
cooperative cancellation and durable random IDs.

Job definitions are runtime dynamic. The compiler marks the jobs runtime
feature, but it does not statically extract `Jobs.define(...)` definitions into
the Plan.

## Storage Adapter

`SchedulerStorage` is provider-neutral at the API boundary and provider-aware
inside SQL generation. The adapter owns:

- schema initialization and version checks
- enqueue and idempotency
- listing and bounded filters
- state transitions and history events
- worker registration and heartbeat
- provider-specific claim SQL
- completion, failure, retry, and dead-letter updates
- recurring schedule storage and bounded tick reads
- distributed locks
- retention cleanup

Multi-step operations run through `db.transaction(callback)` when the provider
exposes it. Scheduler timestamps are read from the provider database clock by
default so workers on different hosts compare leases and due times against the
same time source. Tests can inject a clock through storage options.

## Worker Runtime

`SchedulerWorker` is a cooperative loop:

1. register the worker row
2. heartbeat
3. claim due jobs for configured queues
4. execute up to `concurrency`
5. extend leases when the handler calls `ctx.extendLease()`
6. complete, retry, dead-letter, or timeout the job
7. stop claiming on shutdown and drain in-flight work until timeout

Handlers receive job id, name, queue, attempt number, an abort signal, and an
`extendLease(ms?)` helper.

Timeouts use linked cancellation, abort `ctx.signal`, and fail the job with
`SLOPPY_E_JOBS_TIMEOUT`. Cancellation is cooperative: handlers must observe the
signal to stop their own side effects. Late handler completion after a timeout
is ignored by the worker and must not create a second terminal transition.

## Recurring Scheduler

Recurring schedules are rows in `sloppy_recurring_jobs`. A tick uses the
distributed lock named `sloppy.jobs.recurring.tick` to prevent duplicate enqueue
work across app instances. Each occurrence uses a deterministic idempotency key
based on schedule name and occurrence timestamp.

The tick is bounded by page size and reads due schedules in next-run order.

## Admin Backend

The admin service is a backend object, not a public HTTP surface. It exposes
overview counts, job list/detail, attempts/events, retry/cancel/delete,
recurring pause/resume/trigger, workers, locks, and retention cleanup.

Payload previews are redacted. The admin service does not expose raw payload
inspection.

The native `sloppy jobs` command currently operates SQLite scheduler tables
directly. PostgreSQL and SQL Server live validation uses Sloppy Program Mode
admin workloads against those providers.

## Provider Notes

SQLite is suitable for local and single-node durable scheduling. Claiming runs
inside a transaction and uses conditional updates to avoid double settlement.

PostgreSQL uses row locks with `FOR UPDATE SKIP LOCKED` for claim scans.

SQL Server uses `UPDLOCK`, `READPAST`, and `ROWLOCK` on claim scans.

All provider implementations keep reads bounded with explicit page or batch
sizes.
