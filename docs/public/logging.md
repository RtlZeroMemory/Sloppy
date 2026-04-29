# Logging

Status: Bootstrap memory logging skeleton implemented.

Purpose: document the current deterministic in-memory logging API and the future path to
console/file/native sinks and structured runtime diagnostics.

Implemented API example:

```ts
const builder = Sloppy.createBuilder();

builder.logging.setMinimumLevel("info");
const memorySink = builder.logging.addMemorySink();

const app = builder.build();

app.log.info("hello", { route: "/" });
memorySink.entries();
```

Implemented levels:

- `trace`
- `debug`
- `info`
- `warn`
- `error`

Implemented behavior:

- `builder.logging.setMinimumLevel(level)` sets the minimum recorded level.
- The default minimum level is `info`.
- `builder.logging.addMemorySink()` returns a memory sink.
- `memorySink.entries()` returns frozen entry snapshots.
- Entries below the minimum level are ignored.
- Each entry contains `level`, `message`, and optional `fields`.
- `message` is stored as `String(message)`.
- `fields` is preserved as supplied.
- No timestamp is added so tests remain deterministic.
- Logging with no sinks is allowed and records nothing.
- `builder.build()` freezes further logging configuration.
- `app.log.trace/debug/info/warn/error(...)` writes to configured memory sinks.

Not implemented yet: console sink, file sink, native runtime sink, colors, timestamps,
async logging, structured exporters, log scopes, redaction policy, and native diagnostics
integration.

Related internal docs: `docs/developer-ergonomics.md`.
