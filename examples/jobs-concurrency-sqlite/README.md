# SQLite jobs concurrency

Runs the SQLite durable scheduler validation path through Sloppy Program Mode
with a live SQLite database file. The workload covers idempotent enqueue, lock
single-owner behavior, expired lock takeover, non-owner release failure, and
claim candidate setup. The SQLite live jobs script also launches
`examples/jobs-concurrency-step/main.ts` in multiple Sloppy Program Mode
processes to validate claim uniqueness and expired lease recovery.
