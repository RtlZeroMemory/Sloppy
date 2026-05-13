# Jobs concurrency step

Runs one durable scheduler operation through Sloppy Program Mode. The SQL
Server live jobs script launches this example in multiple `sloppy run`
processes against the same live database to validate idempotent enqueue,
distributed locks, concurrent claims, and expired lease recovery without using
Node as the workload runtime.
