# sloppy jobs

Inspect and operate the durable scheduler tables.

```sh
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

The native command uses the SQLite admin backend. PostgreSQL and SQL Server
durable scheduler validation runs through Sloppy Program Mode live lanes:

```sh
sloppy run examples/jobs-concurrency/main.ts -- postgres
sloppy run examples/jobs-concurrency-step/main.ts -- sqlserver init
```

The SQL Server live script launches the step example in multiple `sloppy run`
processes against the same database for idempotency, lock, claim, and lease
race checks.

## Commands

`init` creates the scheduler schema in the selected SQLite database.

`status` prints counts grouped by job status.

`list` prints recent jobs.

`show <id>` prints one job.

`retry <id>` moves `failed` or `dead` jobs back to `queued`.

`cancel <id>` cancels `queued`, `scheduled`, `processing`, or `retrying` jobs.

`delete <id>` marks `queued`, `dead`, `succeeded`, or `cancelled` jobs as
`deleted`.

`workers` prints durable worker registrations.

`recurring list` prints recurring schedules.

`recurring pause <name>` and `recurring resume <name>` update the schedule
enabled flag.

`recurring trigger <name>` enqueues one immediate job from the stored recurring
payload.

`locks` prints distributed locks.
