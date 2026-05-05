# CORE-TIME-01 Issue Index

Status: CORE-TIME-01.A/B/C/D/G source-of-truth index.

Parent EPIC: #551 CORE-TIME-01 Time, Deadlines, Cancellation, and Scheduling API.

| Slice | Issues | PR intent |
| --- | --- | --- |
| CORE-TIME-01.A/B | #552, #553 | API contract, feature/Plan metadata, diagnostics, and stable JS error classes. |
| CORE-TIME-01.C/D/G | #554, #555, #558 | Native timer backend, owner-thread completion, delay/timeout/deadline/cancellation, and V8/stdlib surface. Implemented in PR 2. |
| CORE-TIME-01.E/F | #556, #557 | Interval/ticker, scheduled jobs, TimeProvider, and FakeClock. |
| CORE-TIME-01.H | #559 | Integration into existing core APIs such as filesystem, app/request lifecycle, provider descriptors, and HTTP policy placeholders. |
| CORE-TIME-01.I | #560 | Conformance, examples, docs, and diagnostic goldens. |

## Feature Names

- Runtime feature id: `stdlib.time`.
- Public import: `sloppy/time`.
- V8 intrinsic namespace: `__sloppy.time`.
- The compiler emits `requiredFeatures: ["stdlib.time"]` when it sees supported named imports
  from `sloppy/time`.

## Stable Diagnostic Codes

- `SLOPPY_E_TIME_TIMEOUT`.
- `SLOPPY_E_TIME_CANCELLED`.
- `SLOPPY_E_TIME_TIMER_DISPOSED`.
- `SLOPPY_E_TIME_INVALID_DELAY`.
- `SLOPPY_E_TIME_DEADLINE_EXPIRED`.
- `SLOPPY_E_TIME_INTERVAL_OVERFLOW`.
- `SLOPPY_E_TIME_SCHEDULE_SKIPPED`.
- `SLOPPY_E_TIME_FAKE_CLOCK_MISUSE`.
- Missing or unavailable `stdlib.time` uses the existing runtime-feature diagnostics.

## Completion Rule

The parent EPIC can close only after #552-#560 are closed and the final PR records evidence
for default gates, V8-gated Time tests where available, conformance/examples/goldens, FS
integration evidence, and the explicit non-goals: no Node timer compatibility, no global
fake timers for ordinary apps, no cron parser, no package-manager behavior, no public alpha
docs, no benchmark claims, and no unrelated network/crypto/process implementation.
