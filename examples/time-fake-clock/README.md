# Time Fake Clock Example

This example documents deterministic tests that
inject a clock explicitly.

`Time.fakeClock()` creates an explicit test-scoped provider. Passing `{ clock }` to
`Time.delay` or `Time.timeout` lets tests advance time deterministically with
`clock.advanceBy(ms)`.

Fake clocks do not mutate global timers for ordinary apps.

## Limitations

This example focuses on deterministic fake-clock behavior for tests.
