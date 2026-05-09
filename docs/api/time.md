# Time

`sloppy/time` is the bootstrap stdlib time and cancellation surface. It owns
delay, timeout, interval, and recurring-job primitives, plus the `Deadline`
and `CancellationController` types that the rest of the stdlib accepts on its
async operations.

## Import

```ts
import {
    Time,
    Deadline,
    CancellationController,
    TimeoutError,
    CancelledError,
    InvalidDeadlineError,
    TimerDisposedError,
} from "sloppy/time";
```

The compiler recognizes `sloppy/time` as a stdlib subpath and emits the
`stdlib.time` runtime feature.

## Current status

API shape is committed. `Time.delay` requires the `__sloppy.time` runtime
bridge; without it the call rejects with `SLOPPY_E_UNAVAILABLE_RUNTIME_FEATURE`.
Pure-JS pieces (`Deadline`, `CancellationController`, `Time.fakeClock`) work
without the bridge.

There is no global `setTimeout` or `setInterval`. All timers are explicit
`Time.*` calls.

## Time

`Time` is a frozen namespace.

| Method | Returns | Notes |
| --- | --- | --- |
| `Time.delay(ms, options?)` | `Promise<void>` | `ms` accepts a number (milliseconds) or a duration string |
| `Time.timeout(opOrPromise, options?)` | `Promise<T>` | wraps a function or promise with a deadline |
| `Time.interval(ms, options?)` | `TimeInterval` (async iterable) | tick stream |
| `Time.every(ms, handler, options?)` | `ScheduledJob` | recurring fire-and-forget job |
| `Time.yield(options?)` | `Promise<void>` | yield to the event loop (zero delay) |
| `Time.systemClock()` | `Clock` | wall-clock + monotonic provider |
| `Time.fakeClock(options?)` | `FakeClock` | manual clock for tests |

Duration strings accept `ms`, `s`, `m`, `h` units (`"500ms"`, `"5s"`,
`"2.5m"`, `"1h"`).

### Common options

```ts
{
  signal?: CancellationSignal | AbortSignal;
  deadline?: Deadline;
  clock?: Clock;
}
```

`Time.timeout` adds `afterMs` for a relative timeout. When both `afterMs` and
`deadline` are provided, the earlier one wins.

`Time.every` adds:

```ts
{
  immediate?: boolean;          // first run without waiting
  noOverlap?: boolean;          // default true; skip if previous still running
  missedRunPolicy?: "skip";     // current option
  maxRuns?: number;
}
```

`Time.interval` adds `immediate?: boolean` and `maxTicks?: number`.

## Deadline

`Deadline` is a frozen factory that produces deadline values stored as
monotonic expiry times (so wall-clock changes don't break them).

| Factory | Result |
| --- | --- |
| `Deadline.after(ms)` | deadline `ms` from now |
| `Deadline.at(unixMsOrDate)` | deadline at an absolute wall-clock instant |
| `Deadline.never()` | a deadline that never expires |

The shape consumed by other stdlib modules is duck-typed: anything with a
`remainingMs()` method is treated as a deadline. In practice always use
`Deadline.*`.

## CancellationController

```ts
const ctl = new CancellationController();
ctl.cancel("user cancelled");
await ctl.signal.throwIfCancelled();
```

Static helpers:

| Helper | Result |
| --- | --- |
| `CancellationController.linked(...signals)` | controller cancelled when any of the source signals is |
| `CancellationController.timeout(ms, options?)` | controller that auto-cancels after `ms` |

The signal exposes `cancelled`, `aborted`, `reason`, `throwIfCancelled()`,
`addEventListener("abort", listener)`, and `removeEventListener(...)`. The
shape is compatible with `AbortSignal` consumers.

## Wall-clock vs monotonic

The `Clock` interface returned by `Time.systemClock()` and `Time.fakeClock()`
is:

```ts
{
  kind: "system" | "fake";
  now(): Date;                  // wall clock
  monotonicNowMs(): number;     // monotonic milliseconds
  delay(ms, options?): Promise<void>;
}
```

Use `Deadline.after(ms)` for elapsed-time deadlines (monotonic) and
`Deadline.at(unixMs)` for absolute instants (wall-clock translated to
monotonic at construction).

## FakeClock

`Time.fakeClock(options?)` returns a manually advanced clock for tests.

```ts
const clock = Time.fakeClock({ now: new Date("2026-01-01T00:00:00Z") });

const delay = Time.delay(1000, { clock }).then(() => "done");
clock.advanceBy(1000);
await delay;
clock.dispose();
```

| Method | Effect |
| --- | --- |
| `clock.set(dateOrUnixMs)` | jump wall-clock |
| `clock.advanceBy(ms)` | advance wall and monotonic time, fire matching timers |
| `clock.now()` | current wall-clock instant |
| `clock.monotonicNowMs()` | current monotonic ms |
| `clock.delay(ms, options?)` | the same delay primitive used by `Time.delay({ clock })` |
| `clock.dispose()` | cancel pending fake timers (`TimerDisposedError`) |

Pass `{ clock }` through to `Time.delay`, `Time.timeout`, `Time.interval`, and
`Time.every` to keep tests deterministic. Examples in
`examples/time-fake-clock` exercise the full pattern.

## Examples

Delay with cancellation:

```ts
import { Time, CancellationController } from "sloppy/time";

const ctl = new CancellationController();
setTimeout(() => ctl.cancel("user-cancelled"), 100);
await Time.delay(1000, { signal: ctl.signal });
```

Timeout wrapping:

```ts
const result = await Time.timeout(async () => doWork(), {
    afterMs: 500,
    signal: ctl.signal,
});
```

Interval iteration:

```ts
for await (const tick of Time.interval(1000, { immediate: true, maxTicks: 3 })) {
    // tick.index, tick.at
}
```

Recurring job:

```ts
const job = Time.every("5s", async (ctx) => {
    if (ctx.signal.cancelled) return;
    await heartbeat();
});

// later
await job.stop();
```

In-repo references:

- `examples/time-basic`
- `examples/time-deadline-cancellation`
- `examples/time-interval-schedule`
- `examples/time-fake-clock`

## Boundaries

- No Node `setTimeout`/`setInterval`. The compiler does not emit them.
- No `process.nextTick` or microtask helpers; use `Time.yield()`.
- Maximum delay is `0xffffffff` ms — larger values throw
  `InvalidDeadlineError`.
- `Time.every` only supports `missedRunPolicy: "skip"` today. Other policy
  strings throw at construction.

## Compiler source-input support

The compiler accepts `import ... from "sloppy/time"` and emits the
`stdlib.time` required feature into the Plan. Aliased and default imports
are rejected by the compiler before the Plan is written.

## Runtime requirements

`Time.delay`, `Time.interval`, and `Time.every` ultimately schedule on the
`__sloppy.time` bridge (a `delay(ms)` function plus an optional
`monotonicMs()`). When the bridge is missing, scheduling rejects with
`SLOPPY_E_UNAVAILABLE_RUNTIME_FEATURE`. `monotonicNowMs` falls back to
`Date.now()` when the bridge omits the optional method.

`Deadline`, `CancellationController`, and `Time.fakeClock` are pure JS and
need no bridge.

## Errors

| Error class | Trigger |
| --- | --- |
| `TimeoutError` | deadline elapsed |
| `CancelledError` | signal aborted |
| `InvalidDeadlineError` | bad `ms`, deadline, or policy input |
| `TimerDisposedError` | controller or fake clock disposed mid-flight |

All four extend `SloppyTimeError` and carry an optional `reason` property.
JS errors from `sloppy/time` are identified by `error.name`. The runtime
diagnostic layer surfaces `SLOPPY_E_TIME_TIMEOUT`,
`SLOPPY_E_TIME_DEADLINE_EXPIRED`, `SLOPPY_E_TIME_CANCELLED`,
`SLOPPY_E_TIME_INVALID_DELAY`, and `SLOPPY_E_TIME_TIMER_DISPOSED` for the
same conditions when reported through `sloppy run` diagnostics.

Bridge-level absence is reported as a plain `Error` whose message starts
with `SLOPPY_E_UNAVAILABLE_RUNTIME_FEATURE`.
