# Time Fake Clock Example

Status: CORE-TIME-01.I source example. This example documents deterministic tests that
inject a clock explicitly.

`Time.fakeClock()` creates an explicit test-scoped provider. Passing `{ clock }` to
`Time.delay` or `Time.timeout` lets tests advance time deterministically with
`clock.advanceBy(ms)`.

Fake clocks do not mutate global timers for ordinary apps. `Time.fakeClock` does not mutate global timers.
This example is not benchmark evidence and does not claim public alpha readiness.
