# Jobs Recurring

Defines a named durable job and a UTC recurring schedule. The recurring tick
uses the scheduler lock service and occurrence idempotency keys.

It opens a real Sloppy SQLite provider with `data.sqlite.open(...)`, initializes
the scheduler schema, registers the recurring schedule, and runs one distributed
tick. Set `SLOPPY_JOBS_SQLITE_PATH` to choose the database path.

Program Mode smoke:

```sh
sloppy run examples/jobs-recurring/main.ts -- ./jobs-recurring.db
```
