# Time Interval and Schedule Example

Status: CORE-TIME-01.I source example. This example documents async iterable intervals and
interval-based scheduled jobs.

`Time.interval` is consumed with `for await`. `Time.every` is interval-based in this EPIC:
it is not a cron parser. Jobs use no-overlap behavior by default, and the documented missed
run policy is `"skip"` so the runtime avoids unbounded catch-up storms.

This example makes no Node timer compatibility promise. It is not a cron parser, has no package-manager behavior,
and makes no benchmark claims.
