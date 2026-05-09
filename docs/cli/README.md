# CLI

The Sloppy CLI is two binaries that ship together:

- `sloppy` — the runtime CLI: builds, runs, inspects, validates.
- `sloppyc` — the standalone compiler. `sloppy build` and `sloppy run`
  invoke it for you; you only need it directly for low-level work.

```
sloppy --help
sloppy --version
```

## Commands

| Command                      | Purpose                                                  |
| ---------------------------- | -------------------------------------------------------- |
| [`sloppy build`](build.md)   | Compile source into Plan-backed artifacts                |
| [`sloppy run`](run.md)       | Run a compiled app or compile-and-run from source        |
| [`sloppy routes`](routes.md) | List route metadata from a Plan                          |
| [`sloppy capabilities`](capabilities.md) | List declared capabilities                   |
| [`sloppy doctor`](doctor.md) | Validate the local environment and a Plan                |
| [`sloppy audit`](audit.md)   | Run security/compliance checks against a Plan            |
| [`sloppy openapi`](openapi.md) | Generate an OpenAPI document from a Plan               |

`build` and `run` are what you'll use day to day. The rest are inspection
tools that work against an already-built `app.plan.json`.

## Project mode vs explicit paths

Most commands work in two ways: read `sloppy.json` from the current directory
(project mode), or take explicit paths.

```
# project mode — uses sloppy.json
sloppy build
sloppy run

# explicit
sloppy build src/main.ts --out dist
sloppy run --artifacts dist
sloppy run src/main.ts
```

See [reference/sloppy-json.md](../reference/sloppy-json.md) for the project
config schema.

## Defaults

| Flag              | Default        |
| ----------------- | -------------- |
| `--host`          | `127.0.0.1`    |
| `--port`          | `5173`         |
| `--environment`   | `Development`  |
| `--out` / `outDir`| `.sloppy`      |

`--host` and `--port` apply to `sloppy run`; `sloppy build` accepts them so
they can be baked into the Plan but doesn't open a socket. `--environment`
selects which `appsettings.{Environment}.json` overlay applies.

## Exit codes

| Code | Meaning                                  |
| ---- | ---------------------------------------- |
| 0    | Success                                  |
| 1    | Recoverable error (bad flags, validation, missing files) |
| Other| Internal error from the runtime          |

Diagnostics print to stderr; structured output (when `--format json` is
available) goes to stdout.
