# Jobs PostgreSQL Worker

Shows the shape for a dedicated PostgreSQL-backed scheduler worker. The worker
claims jobs with PostgreSQL row locks and processes the `maintenance` queue.

This example needs a real PostgreSQL provider connection and matching
configuration. Set `SLOPPY_JOBS_POSTGRES_URL` before running it.
