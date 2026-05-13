# sloppy jobs

Inspect and operate durable scheduler tables.

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
durable scheduler validation runs through Sloppy Program Mode live lanes, where
application code opens the matching provider and uses `Jobs.create(...)`.

## Commands

| Command | Purpose |
| --- | --- |
| `init` | Create the scheduler schema in the selected SQLite database |
| `status` | Print counts grouped by job status |
| `list` | Print recent jobs |
| `show <id>` | Print one job |
| `retry <id>` | Move `failed` or `dead` jobs back to `queued` |
| `cancel <id>` | Cancel `queued`, `scheduled`, `processing`, or `retrying` jobs |
| `delete <id>` | Mark `queued`, `dead`, `succeeded`, or `cancelled` jobs as `deleted` |
| `workers` | Print durable worker registrations |
| `recurring list` | Print recurring schedules |
| `recurring pause <name>` | Disable a recurring schedule |
| `recurring resume <name>` | Enable a recurring schedule |
| `recurring trigger <name>` | Enqueue one immediate job from the stored recurring payload |
| `locks` | Print distributed locks |

The command expects an existing or creatable SQLite database path. It does not
connect directly to PostgreSQL or SQL Server.

## Live Provider Scripts

Optional live lanes exercise the public Jobs API with real providers:

```powershell
.\tools\windows\test-live-jobs-sqlite.ps1
.\tools\windows\test-live-jobs-postgres.ps1
.\tools\windows\test-live-jobs-sqlserver.ps1
.\tools\windows\test-live-jobs.ps1 -Provider all
```

Those scripts require the provider bridge and database service for the selected
provider. They are validation tools, not required for normal SQLite scheduler
admin commands.
