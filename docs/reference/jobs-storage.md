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

## `sloppy_jobs`

Required columns:

| Column | Meaning |
| --- | --- |
| `id` | Stable job id |
| `name` | Job definition name |
| `queue` | Queue name |
| `status` | Current state |
| `payload_json` | JSON payload |
| `payload_schema` | Payload schema name/kind when known |
| `priority` | Claim order; higher first |
| `run_at` | Earliest claim time |
| `created_at`, `updated_at` | ISO UTC timestamps |
| `locked_by`, `locked_until` | Current worker lease |
| `attempt_count`, `max_attempts` | Retry accounting |
| `retry_policy_json` | Stored retry policy |
| `next_retry_at` | Next retry time |
| `last_error_code`, `last_error_message` | Last failure summary |
| `diagnostic_id`, `correlation_id` | Diagnostic correlation |
| `idempotency_key` | Optional unique enqueue key |
| `timeout_ms` | Handler timeout |
| `metadata_json` | Operational metadata |

Scheduler-created IDs are random durable IDs with stable prefixes. Timestamps
stored by scheduler operations come from the provider database clock unless a
test clock is explicitly injected into the storage adapter.

## Provider Claim Strategy

Workers first move expired `processing` leases back to `queued`, then move due
`scheduled` and `retrying` rows to `queued`, then claim due queued rows.

| Provider | Claim strategy |
| --- | --- |
| SQLite | Transactional `select ... order by ... limit ?` followed by conditional `update ... where status = 'queued'` |
| PostgreSQL | Transactional `select ... for update skip locked limit $n` followed by conditional update |
| SQL Server | Transactional `select top (?) ... with (updlock, readpast, rowlock)` followed by conditional update |

Claim order is:

1. queue filter
2. `status = 'queued'`
3. `run_at <= now`
4. `priority desc`
5. `run_at asc`
6. `created_at asc`

Each successful claim creates a `sloppy_job_attempts` row and a
`sloppy_job_events` row.

## Indexes

The schema creates indexes for:

- queue/status/run time/lock time/priority claim scans
- status and update-time views
- name and creation-time views
- worker lease lookup
- attempts by job and attempt number
- events by job and creation time
- recurring schedules by enabled state and next run time
- workers by status and heartbeat
- filtered idempotency uniqueness when `idempotency_key` is present
- recurring schedule name uniqueness

SQL Server uses `sys.indexes` guards because `create index if not exists` is
not SQL Server syntax.

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
schedule without enqueueing a job.

The public cron parser supports five fields and UTC. The storage model includes
the `timezone` column so schedule metadata stays explicit.

## Locks

`sloppy_job_locks` stores:

- `name`
- `owner`
- `locked_until`
- `updated_at`

An acquire succeeds when no row exists, the row has expired, or the existing
row is already owned by the caller. Release and extend require the same owner.

## Retention Cleanup

`admin.cleanup()` reads a bounded batch of terminal jobs and moves eligible
rows to `deleted`. It does not delete active `scheduled`, `queued`,
`processing`, or `retrying` jobs.

Options:

| Option | Meaning |
| --- | --- |
| `keepSucceededMs` | Minimum age before succeeded jobs are deleted |
| `keepDeadMs` | Minimum age before dead jobs are deleted |
| `keepCancelledMs` | Minimum age before cancelled jobs are deleted |
| `batchSize` | Bounded cleanup batch size |

## Redaction

Admin job objects expose `payloadPreview`, not raw payload, for routine
inspection. The preview redacts common secret-bearing keys and definition
specific redaction keys.
