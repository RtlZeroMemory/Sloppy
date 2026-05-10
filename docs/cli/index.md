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
| [`sloppy create`](create.md) | Copy a built-in app template into a new directory        |
| [`sloppy build`](build.md)   | Compile source into Plan-backed artifacts                |
| [`sloppy package`](package.md) | Build a directory app package from source artifacts    |
| [`sloppy run`](run.md)       | Run a compiled app or compile-and-run from source        |
| [`sloppy routes`](routes.md) | List route metadata from a Plan                          |
| [`sloppy deps`](deps.md)     | Inspect bundled package, module, asset, and Node shim metadata |
| [`sloppy capabilities`](capabilities.md) | List declared capabilities                   |
| [`sloppy doctor`](doctor.md) | Validate the local environment and a Plan                |
| [`sloppy audit`](audit.md)   | Run security/compliance checks against a Plan            |
| [`sloppy openapi`](openapi.md) | Generate an OpenAPI document from a Plan               |
| [`sloppyc`](sloppyc.md)      | Run the compiler directly                                |

`create`, `build`, `run`, and `package` are the normal app workflow. The
inspection tools work against an already-built `app.plan.json`.

## Project mode vs explicit paths

Most commands work in two ways: read `sloppy.json` from the current directory
(project mode), or take explicit paths.

```
# project mode — uses sloppy.json
sloppy build
sloppy run
sloppy package

# explicit
sloppy build src/main.ts
sloppy run src/main.ts
sloppy run .sloppy
sloppy package src/main.ts
sloppy openapi .sloppy
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

`sloppy create` defaults to `api`. The current public template names are
`api`, `minimal-api`, `program`, `cli`, `package-api`, and `node-compat`.

## Exit codes

| Code | Meaning                                  |
| ---- | ---------------------------------------- |
| 0    | Success                                  |
| 1    | Recoverable CLI/runtime error, or Program Mode `main` returned `1`; check stderr for the source |
| 2-255| Program Mode `main` returned that exit code, or the runtime hit an internal error |

Diagnostics print to stderr; structured output (when `--format json` is
available) goes to stdout.
