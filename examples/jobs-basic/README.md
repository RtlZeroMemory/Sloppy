# Jobs Basic

Defines a durable `send-email` job, validates payloads with `Schema`, configures
retry and timeout policy, and enqueues with an idempotency key.

It opens a real Sloppy SQLite provider with `data.sqlite.open(...)`, initializes
the scheduler schema, runs one worker pass, and exports the admin backend
service. Set `SLOPPY_JOBS_SQLITE_PATH` to choose the database path.

Program Mode smoke:

```sh
sloppy run examples/jobs-basic/main.ts -- ./jobs-basic.db
```
