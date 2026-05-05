# Time API Architecture

Status: CORE-TIME-01.A/B/C/D/G source of truth. This document defines the intended first
`sloppy/time` API contract, feature metadata, diagnostics, implementation boundaries, and
the first V8-backed runtime surface. The examples in this document are illustrative
contract examples for the current API shape. Native delay, timeout, deadline, cancellation,
and V8 owner-thread Promise settlement are implemented; intervals, scheduled jobs, fake
clocks, FS integration, executable examples, and final conformance land in later
CORE-TIME-01 slices. The interval, scheduled-job, and fake-clock contract semantics are
locked here but their runtime implementation remains deferred.

## Goals

- Provide async-first time, deadline, cancellation, interval, scheduled-job, and test-clock
  APIs for Sloppy applications.
- Make deadlines and cancellation reusable across filesystem, provider, HTTP, networking,
  process, and app/request lifecycle work.
- Keep timer completions owner-thread safe: timer callbacks never enter V8 directly, and
  JavaScript Promise settlement happens through the V8 owner-thread scheduler.
- Keep fake clocks explicit and test-scoped instead of mutating global runtime time.

## Public Module

Applications import the API from `sloppy/time`:

```ts
import { Time, Deadline, CancellationController } from "sloppy/time";
```

The runtime feature descriptor is `stdlib.time`. The compiler emits that feature only for
non-type, non-namespace named imports from `sloppy/time`, such as
`import { Time } from "sloppy/time"`. Namespace imports, default imports, side-effect
imports, and TypeScript type-only imports do not activate the feature in the Plan. The
V8 bridge namespace is `__sloppy.time` and is registered only when the active Plan enables
`stdlib.time`.

## API Shape

```ts
await Time.delay(500);
await Time.delay(500, { signal: ctx.signal });

const result = await Time.timeout(async (signal) => {
  return await loadUser({ signal });
}, { afterMs: 5000, signal: ctx.signal });

const deadline = Deadline.after(5000);
await File.readText("data:/big.txt", { deadline });

const controller = new CancellationController();
controller.cancel("no longer needed");

for await (const tick of Time.interval(1000, { signal: ctx.signal })) {
  await doWork();
}

const job = Time.every("5m", async (ctx) => {
  await cleanup({ signal: ctx.signal });
}, { noOverlap: true, missedRunPolicy: "skip" });

const clock = Time.fakeClock();
const p = Time.delay(1000, { clock });
clock.advanceBy(1000);
await p;
```

## Options

- `DelayOptions`: `signal`, `deadline`, `clock`.
- `TimeoutOptions`: `afterMs`, `deadline`, `signal`, `clock`.
- `IntervalOptions`: `signal`, `deadline`, `clock`, `immediate`, `maxTicks`,
  `mode: "fixed-delay" | "fixed-rate"` where fixed-delay is the default.
- `ScheduleOptions`: `signal`, `clock`, `noOverlap`, `missedRunPolicy: "skip"`,
  optional `immediate`, and optional `maxRuns`.

`signal` means caller cancellation. `deadline` means shared time budget. When both are
present, the first terminal state wins and cleanup still runs exactly once. `clock` is an
explicit provider override; ordinary app timers use the system clock.

## Semantics

- `Time.delay(ms, options?)` resolves once after a non-negative finite delay, or rejects
  with `CancelledError`, `TimeoutError`, `InvalidDeadlineError`, or `TimerDisposedError`.
- `Time.timeout(operationOrPromise, options)` supports Promise convenience form and
  function-with-signal form. Only the function form can propagate cancellation into user
  work; the Promise form may stop waiting without claiming to cancel arbitrary Promises.
- `Deadline.after(ms)`, `Deadline.at(dateOrUnixMs)`, and `Deadline.never()` create immutable
  deadline values with remaining/expired checks. Durations use monotonic time where the
  backend can provide it; wall time is not used for timeout accounting.
- `CancellationController` supports explicit reason, linked signals/controllers,
  timeout-backed cancellation, and safe disposal.
- `Time.interval(ms)` is an async iterable. It is no-overlap fixed-delay by default and
  never builds an unbounded catch-up queue.
- `Time.every(interval, handler, options)` creates an interval-based `ScheduledJob` with
  `pause`, `resume`, `stop`, `nextRun`, and running-state inspection. Cron parsing is out
  of scope for this EPIC.
- `Time.systemClock()` returns the normal provider. `Time.fakeClock()` returns an explicit
  test-scoped provider that drives delay, timeout, interval, and scheduled jobs only for
  operations that receive it.

## Error Classes

- `TimeoutError` for runtime deadline/timeout expiry.
- `CancelledError` for caller/app/request cancellation.
- `InvalidDeadlineError` for invalid or already-expired deadline inputs where the operation
  requires schedulable time.
- `TimerDisposedError` for disposed timer, interval, job, or fake-clock handles.

## Diagnostics

Stable Time diagnostic names:

- `SLOPPY_E_TIME_TIMEOUT`;
- `SLOPPY_E_TIME_CANCELLED`;
- `SLOPPY_E_TIME_TIMER_DISPOSED`;
- `SLOPPY_E_TIME_INVALID_DELAY`;
- `SLOPPY_E_TIME_DEADLINE_EXPIRED`;
- `SLOPPY_E_TIME_INTERVAL_OVERFLOW`;
- `SLOPPY_E_TIME_SCHEDULE_SKIPPED`;
- `SLOPPY_E_TIME_FAKE_CLOCK_MISUSE`.

Missing or inactive `stdlib.time` uses the runtime-feature diagnostics
`SLOPPY_E_UNKNOWN_RUNTIME_FEATURE`, `SLOPPY_E_UNAVAILABLE_RUNTIME_FEATURE`, or
`SLOPPY_E_RUNTIME_FEATURE_DEPENDENCY_MISSING`.

## Runtime Invariants

- Timer callbacks/completions must not enter V8 directly.
- Promise settlement must happen through the V8 owner-thread scheduler.
- No raw native timer, clock, or cancellation handles are exposed to JavaScript.
- Cross-domain data must be copied or owned before scheduling.
- Cleanup runs exactly once across success, cancellation, timeout, disposal, shutdown, and
  late completion.
- Late completions check terminal state and become cleanup-only.
- Evidence lanes remain separate: default, V8-gated, package, live-provider,
  stress/torture, and benchmark.

## Implemented Runtime Surface

CORE-TIME-01.C/D/G implements the first runtime-backed subset:

- `__sloppy.time.delay(ms)` records due requests on a shared Time scheduler for the V8
  backend. That scheduler never enters V8; it posts owned timer completions through
  `SlAsyncLoop`, and the owning isolate thread resolves the Promise.
- `__sloppy.time.monotonicMs()` exposes monotonic milliseconds to the stdlib wrapper for
  deadline accounting.
- `stdlib/sloppy/time.js` and `runtime-classic.js` implement `Time.delay`, `Time.timeout`,
  `Time.yield`, `Deadline.after`, `Deadline.at`, `Deadline.never`, and
  `CancellationController`.
- Missing or inactive `stdlib.time` remains fail-closed; the private `__sloppy.time`
  namespace is not registered unless the active Plan enables `stdlib.time`.

The native delay primitive does not expose timer handles to JavaScript. JavaScript
deadline/cancellation wrappers settle the user-facing Promise deterministically; later
timer completions become cleanup-only on the native side.

## Non-Goals

- No Node timer compatibility promise.
- No callback-first timer API as the recommended style.
- No global fake timers for ordinary apps.
- No cron parser in CORE-TIME-01.
- No benchmark/performance claims.
- No public alpha docs.
- No unrelated filesystem, network, crypto, process, provider, or package-manager
  implementation.
