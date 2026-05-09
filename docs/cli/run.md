# `sloppy run`

Run a Sloppy app — either as a long-lived HTTP server or as a single
synthetic request. `run` enters V8, so it requires a V8-enabled build.

```
sloppy run [source | --artifacts <dir>] [--stdlib <dir>]
           [--environment <name>] [--host <ip>] [--port <n>]
           [--once METHOD TARGET]
```

## Modes

**Project mode** (no positional, no `--artifacts`):

```
sloppy run
```

Reads `sloppy.json`, compiles, then runs the artifacts.

**Source input**:

```
sloppy run src/main.ts
```

Compiles the supplied source through `sloppyc` into the source-input cache,
validates artifacts, then runs.

**Pre-built artifacts**:

```
sloppy run --artifacts .sloppy
```

Loads `app.plan.json`, `app.js`, and `app.js.map` from the directory and
runs them directly.

## One-shot requests

`--once METHOD TARGET` runs a single synthetic request through the runtime
and exits. Use it for smoke tests:

```
sloppy run --artifacts .sloppy --once GET /health
sloppy run src/main.ts --once POST /users
```

The response body prints to stdout. Exit status is 0 if the handler
returned successfully; otherwise non-zero with a diagnostic.

`--once` doesn't open a listener, so `--host` and `--port` are ignored.

## Flags

| Flag                   | Default         | Purpose                                              |
| ---------------------- | --------------- | ---------------------------------------------------- |
| `--artifacts <dir>`    | —               | Load already-built artifacts                         |
| `--stdlib <dir>`       | bundled         | Override the bootstrap stdlib path                   |
| `--environment <name>` | `Development`   | Environment name + overlay selection                 |
| `--host <ip>`          | `127.0.0.1`     | Server bind host                                     |
| `--port <n>`           | `5173`          | Server bind port (1–65535)                           |
| `--once METHOD TARGET` | —               | Run one synthetic request and exit                   |

`--artifacts` and a positional source path are mutually exclusive — pick
one.

## Examples

```
# Build and run on the default host/port.
sloppy run

# Run already-built artifacts.
sloppy run --artifacts .sloppy

# Bind to all interfaces, custom port.
sloppy run --artifacts .sloppy --host 0.0.0.0 --port 8080

# Smoke a single request.
sloppy run src/main.ts --once GET /health

# Use a non-Development environment overlay.
sloppy run --environment Staging
```

## What happens at startup

1. Parse CLI flags and resolve project config.
2. If source input, invoke `sloppyc build` and write artifacts.
3. Read `app.plan.json`, validate schema, hashes, and required features.
4. Stage the bootstrap stdlib.
5. Initialize the V8 isolate and engine bridge.
6. Evaluate the artifact bundle and register handlers.
7. Build the route table and start the listener (or run the `--once`
   request).

If any step fails the runtime exits with a diagnostic and a non-zero status.

## Shutdown

`Ctrl+C` (SIGINT) requests shutdown. The runtime stops accepting new
connections, drains active request lifecycles within configured limits, and
exits. There is no graceful drain for in-flight production traffic — for
that, terminate behind a reverse proxy that handles connection draining.
