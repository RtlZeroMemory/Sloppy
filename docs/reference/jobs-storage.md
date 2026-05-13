# Jobs Storage

Sloppy jobs use six scheduler tables plus one schema-version table. The schema
is initialized by `Jobs.storage.*(db).init()` and is provider-specific for
SQLite, PostgreSQL, and SQL Server.

## Tables

| Table | Purpose |
| --- | --- |
| `sloppy_job_schema` | Scheduler schema version and update timestamp |
| `sloppy_jobs` | Durable job instances |
| `sloppy_job_attempts` | One row per execution attempt |
| `sloppy_recurring_jobs` | Recurring schedule definitions |
| `sloppy_job_workers` | Worker registrations and heartbeat state |
| `sloppy_job_locks` | Scheduler-backed distributed locks |
| `sloppy_job_events` | Job event/history records |

## Job Rows

`sloppy_jobs` stores the durable queue item: id, job name, queue, status,
payload JSON, priority, run time, attempt count, retry policy, timeout,
correlation fields, idempotency key, lock owner, lock expiry, and safe metadata.

Scheduler-created IDs are random durable IDs with stable prefixes. Timestamps
stored by scheduler operations come from the provider database clock unless a
test clock is explicitly injected into the storage adapter.

## Claim Strategy

Workers first move expired `processing` leases back to `queued`, then move due
`scheduled` and `retrying` rows to `queued`, then claim due queued rows.

| Provider | Claim strategy |
| --- | --- |
| SQLite | Transactional `select ... order by ... limit ?` followed by conditional update |
| PostgreSQL | Transactional `select ... for update skip locked limit $n` followed by conditional update |
| SQL Server | Transactional `select top (?) ... with (updlock, readpast, rowlock)` followed by conditional update |

Claim order is queue filter, `queued` status, `run_at <= now`, priority
descending, run time ascending, and creation time ascending. Each successful
claim creates attempt and event rows.

## State Transitions

Allowed transitions:

| From | To |
| --- | --- |
| `scheduled` | `queued`, `cancelled` |
| `queued` | `processing`, `cancelled`, `deleted` |
| `processing` | `succeeded`, `failed`, `cancelled` |
| `failed` | `retrying`, `dead`, `queued` |
| `retrying` | `queued`, `cancelled` |
| `dead` | `queued`, `deleted` |
| `succeeded` | `deleted` |
| `cancelled` | `deleted` |

Every transition writes a `sloppy_job_events` row. A repeated transition to the
same state returns the current job row without adding a duplicate event.

## Recurring Tick

`tickRecurring` acquires `sloppy.jobs.recurring.tick` in `sloppy_job_locks`.
The tick reads a bounded set of due enabled recurring jobs, applies the
schedule's misfire policy, then updates `last_run_at` and `next_run_at`.

`run-once` enqueues one occurrence, `catch-up-limited` enqueues bounded missed
occurrences using per-occurrence idempotency keys, and `ignore` advances the
schedule without enqueueing a job. The public cron parser supports five fields
and UTC.

## Locks

`sloppy_job_locks` stores `name`, `owner`, `locked_until`, and `updated_at`.
An acquire succeeds when no row exists, the row has expired, or the existing
row is already owned by the caller. Release and extend require the same owner.

## Retention And Redaction

`admin.cleanup()` reads a bounded batch of terminal jobs and moves eligible rows
to `deleted`. It does not delete active `scheduled`, `queued`, `processing`, or
`retrying` jobs.

Admin job objects expose `payloadPreview`, not raw payload, for routine
inspection. The preview redacts common secret-bearing keys and definition
specific redaction keys.
