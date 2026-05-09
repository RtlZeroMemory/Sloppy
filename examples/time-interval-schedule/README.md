# Time Interval and Schedule Example

This example documents async iterable intervals and
interval-based scheduled jobs.

`Time.interval` is consumed with `for await`. `Time.every` is interval-based and is not a
cron parser. Jobs use no-overlap behavior by default, and the documented missed-run policy
is `"skip"` so the runtime avoids unbounded catch-up storms.

## Limitations

This example is interval-based. Cron parsing and benchmark work are outside this
example.
