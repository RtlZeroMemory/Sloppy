# Jobs concurrency

Runs a Sloppy Program Mode durable scheduler workload against SQLite or
PostgreSQL.

```powershell
sloppy run examples/jobs-concurrency/main.ts -- sqlite .sloppy/jobs-concurrency.db
```

For PostgreSQL set `SLOPPY_JOBS_POSTGRES_URL`. SQL Server live concurrency uses
`examples/jobs-concurrency-step/main.ts` from the Windows live jobs script so
the race happens across multiple Sloppy Program Mode processes.
