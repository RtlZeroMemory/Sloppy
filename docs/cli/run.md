# `sloppy run`

Run a Sloppy app — either as a long-lived HTTP server or as a single
synthetic request. `run` enters V8, so it requires a V8-enabled build.

```text
sloppy run [source | artifacts-dir | package-dir | --artifacts <dir>] [--stdlib <dir>]
           [--environment <name>] [--host <ip>] [--port <n>]
           [--kind web|program]
           [--once METHOD TARGET] [-- <program-args...>]
```

## Modes

**Project mode** (no positional, no `--artifacts`):

```sh
sloppy run
```

Reads `sloppy.json`, compiles, then runs the artifacts.

**Source input**:

```sh
sloppy run src/main.ts
```

Compiles the supplied source through `sloppyc` into `.sloppy`, validates
artifacts, then runs.

**Pre-built artifacts**:

```sh
sloppy run .sloppy
```

Loads `app.plan.json`, `app.js`, and `app.js.map` from the directory and
runs them directly. `sloppy run --artifacts .sloppy` is the explicit form for
scripts that prefer named flags.

**Package directory**:

```sh
sloppy run .sloppy/package -- one two
```

Loads `.sloppy/package/manifest.json`, then runs the copied artifacts from
`.sloppy/package/artifacts`. The current runner expects the canonical
`artifacts/app.plan.json`, `artifacts/app.js`, and `artifacts/app.js.map`
layout recorded by `sloppy package`; it does not resolve arbitrary manifest
artifact paths. Package runs do not need the original source checkout.

## One-shot requests

`--once METHOD TARGET` runs a single synthetic request through the
runtime and exits. Use it for smoke tests:

```sh
sloppy run .sloppy --once GET /health
sloppy run src/main.ts --once POST /users
```

The full HTTP response is written to stdout — status line, response
headers, blank line, and body — using the same serializer the real
HTTP transport uses. Pipe through `head` if you only want the status
line.

```http
$ sloppy run .sloppy --once GET /health
HTTP/1.1 200 OK
content-type: text/plain; charset=utf-8
content-length: 2

ok
```

Exit status is 0 if the dispatch produced a response (including
mapped error responses like 404). It is non-zero only on internal
failures (parse, V8 init, target validation).

`--once` doesn't open a listener, so `--host` and `--port` are ignored.
It currently creates a minimal synthetic request from only the method and
target. There are no CLI flags yet for request headers or body bytes. Use the
app test host for handler tests that need JSON bodies, request headers, CORS
preflight detail, or request-scope cleanup without entering V8.

## Flags

| Flag                   | Default         | Purpose                                              |
| ---------------------- | --------------- | ---------------------------------------------------- |
| `--artifacts <dir>`    | —               | Explicitly load already-built artifacts              |
| `--stdlib <dir>`       | bundled         | Override the bootstrap stdlib path                   |
| `--environment <name>` | `Development`   | Environment name + overlay selection                 |
| `--host <ip>`          | `127.0.0.1`     | Server bind host                                     |
| `--port <n>`           | `5173`          | Server bind port (1–65535)                           |
| `--kind web\|program`  | inferred for direct source, `web` for project mode without `kind` | Override source kind |
| `--once METHOD TARGET` | —               | Run one synthetic request and exit                   |
| `-- <program-args...>` | empty           | Arguments passed to Program Mode `main(args, ctx)`   |

`--artifacts` and a positional source or artifact path are mutually exclusive.
Arguments after `--` are valid only for Program Plans.

## Program Mode

Program Plans run a route-free `main`/default export instead of preparing a web
route table. `--once` is web-only and is rejected for Program Plans. A non-V8
build can compile and inspect Program Mode artifacts, but `sloppy run` still
fails before execution with the same V8 required-feature diagnostic used by web
artifacts.

The current Program runtime calls a named `main` export first, then a default
function export, then relies on top-level module execution. `main` and default
functions receive `(args, ctx)`, where `args` is the list after `--` and `ctx`
contains `kind`, `args`, `cwd`, `environment`, and
`plan.metadataCompleteness`.

```sh
sloppy run src/main.ts -- --name Ada
sloppy run --artifacts .sloppy -- --name Ada
sloppy run .sloppy/package -- --name Ada
```

`console.log`, `console.info`, and `console.debug` write to stdout.
`console.warn` and `console.error` write to stderr. Returning an integer from
`0` through `255` sets the process exit code. Throwing, rejecting, or returning
an out-of-range exit code exits non-zero with a diagnostic. Program console
output is currently buffered until the Program entrypoint completes.

## Logging Config

`sloppy run` creates the native logging runtime from Plan/config metadata.
Console logging is enabled by default. Configure it in `appsettings.json`:

```json5
{
  "logging": {
    "minimumLevel": "info",
    "queueCapacity": 64,
    "console": {
      "enabled": true,
      "format": "pretty"
    },
    "file": {
      "path": "app.jsonl",
      "format": "jsonl"
    }
  }
}
```

Supported console formats are `pretty` and `jsonl`. The file sink writes JSONL
only, opens in append mode, and expects the parent directory to exist.

## Examples

```sh
# Build and run on the default host/port.
sloppy run

# Run already-built artifacts.
sloppy run .sloppy

# Bind to all interfaces, custom port.
sloppy run .sloppy --host 0.0.0.0 --port 8080

# Smoke a single request.
sloppy run src/main.ts --once GET /health

# Use a non-Development environment overlay.
sloppy run --environment Staging
```

## What happens at startup

1. Parse CLI flags and resolve project config.
2. If source input, invoke `sloppyc build` and write artifacts.
3. Read `app.plan.json`, validate schema, hashes, target metadata, and required features.
4. Prepare capabilities, server config, logging config, and the Plan-backed route table for web Plans.
5. Stage the bootstrap stdlib.
6. Initialize the native logging runtime.
7. Initialize the V8 isolate and engine bridge.
8. Evaluate the artifact bundle and register handlers or the Program entrypoint.
9. Start the listener, run the `--once` request, or call the Program entrypoint.

If any step fails the runtime exits with a diagnostic and a non-zero status.

## Shutdown

For web servers, `Ctrl+C` (SIGINT) requests shutdown. The runtime stops accepting new
connections, flushes configured logging sinks, and exits. Production-style
graceful drain (long timeouts, in-flight connection completion, signaled
load-balancer drain) is not implemented today — terminate behind a reverse proxy
that handles connection draining for that.
