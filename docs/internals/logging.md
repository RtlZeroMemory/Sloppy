# Logging Runtime

Sloppy logging is a native runtime subsystem. The JavaScript API describes the
event a user wants to write; the C runtime owns event storage, redaction,
queueing, sink dispatch, flushing, and shutdown.

## Layers

```text
JS app/ctx logger
  -> V8 bridge field conversion
  -> SlLogEventBuilder
  -> SlLogRuntime bounded queue
  -> dispatcher thread
  -> memory / console / file / custom sinks
```

The public C surface is `include/sloppy/logging.h`. The implementation lives in
`src/core/logging.c`.

## Event Model

`SlLogEvent` uses fixed-size storage:

- level, timestamp, and monotonic sequence;
- category;
- message;
- request ID;
- route name and route pattern;
- up to `SL_LOG_MAX_FIELDS` structured fields.

Field keys and string values are copied into the event. Supported field kinds
are null, boolean, int64, double, string, and preformatted JSON fragments for
native callers. The V8 bridge currently accepts shallow scalar JS fields and
rejects unsupported values before they reach native sinks.

## Redaction

`sl_log_event_apply_redaction` runs before queue submission finishes. Default
sensitive-key matching is case-insensitive and covers common variants of:

- password / passwd / pwd;
- secret / token;
- authorization / cookie / set-cookie;
- apiKey / api_key;
- clientSecret / client_secret;
- privateKey / private_key;
- passphrase;
- connectionString / connection_string.

Runtime config can provide extra redaction keys. Redacted fields preserve their
field names and replace the value with `[REDACTED]`.

## Queue And Backpressure

`SlLogRuntime` owns a bounded ring queue protected by a platform mutex and
condition variable. `sl_log_runtime_submit` is the request-path admission API:

- disabled levels return without event enqueue;
- events are copied before admission;
- queue capacity is fixed at runtime creation;
- drop-new is the default backpressure policy;
- drop-oldest is available for callers that choose it;
- submitted, dispatched, dropped, and sink-failure counters are observable in
  `SlLogRuntimeSnapshot`.

The dispatcher thread fans accepted events out to sinks. `flush` waits until the
queue and in-flight sink writes are drained, then flushes each sink that exposes
a flush callback.

## Sinks

Memory sink:

- bounded ring buffer for tests and bridge inspection;
- tracks overwritten entries separately from runtime queue drops.

Console sink:

- pretty text or JSONL formatting;
- writes through a caller-provided byte writer.

File sink:

- JSONL only;
- opens in append mode;
- uses an owned buffer instead of opening per event;
- requires the parent directory to exist;
- flushes explicitly and during shutdown;
- reports write/flush failures through sink failure counters and flush status.

Custom sinks:

- use Sloppy-owned callbacks for tests and future adapters;
- receive redacted native events.

## Lifecycle

`sloppy run` creates the logging runtime before the engine bridge and registers
shutdown cleanup after engine creation. Cleanup order destroys the engine first,
then flushes and shuts down logging. That keeps `ctx.log` callbacks from racing
against closed sinks while still flushing queued events at app shutdown.

## Tests And Benchmarks

Native tests:

```powershell
ctest --test-dir build/windows-dev -R "core.logging.structured|stress.logging.structured" --output-on-failure
```

Benchmark smoke:

```powershell
build/windows-dev/sloppy_bench.exe --smoke --format json --bench logging.enabled.five_fields
```

V8 bridge coverage is under `engine.v8.smoke`. Benchmarks are engineering
feedback only; correctness comes from the unit, stress, bootstrap, and V8 lanes.
