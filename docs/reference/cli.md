# CLI Reference

Sloppy has two CLIs:

- `sloppy`: build/run plus Plan inspection (`routes`, `capabilities`, `doctor`, `audit`, `openapi`).
- `sloppyc`: source compiler that emits `app.plan.json`, `app.js`, and `app.js.map`.

## sloppy

```text
sloppy --help
sloppy --version
sloppy build [source.js|source.mjs|source.ts] [--out <dir>] [--environment <name>] [--host <host>] [--port <port>]
sloppy run [source.js|source.mjs|source.ts|--artifacts <dir>] [--stdlib <dir>] [--environment <name>] [--host <host>] [--port <port>] [--once METHOD TARGET]
sloppy routes --plan <path> [--format text|json]
sloppy capabilities --plan <path> [--format text|json]
sloppy doctor [--plan <path>] [--format text|json]
sloppy audit --plan <path> [--format text|json]
sloppy openapi --plan <path> [--output <path>]
```

### sloppy build

Input modes:

| Mode | Source | Output directory |
| --- | --- | --- |
| `sloppy build <source>` | positional source file | `--out` if provided, else `.sloppy/cache/dev/source-input` |
| `sloppy build` | `sloppy.json` `entry` | `sloppy.json` `outDir` |

Notes:

- Supported source extensions: `.js`, `.mjs`, `.ts`.
- `--artifacts` is rejected for `build`.
- `--out` is rejected when source comes from `sloppy.json`.
- `--port` must parse as `1..65535`.
- `--host` and `--port` are forwarded to compiler config.
- `build` compiles and validates artifacts but does not execute handlers.

### sloppy run

Input modes:

| Mode | Behavior |
| --- | --- |
| `sloppy run --artifacts <dir>` | Run existing artifacts. |
| `sloppy run <source>` | Compile source, then run emitted artifacts. |
| `sloppy run` | Load `sloppy.json`, compile `entry`, then run. |

`sloppy run <source.js|source.mjs|source.ts>` invokes `sloppyc build`, validates
the emitted artifacts, and then enters the same runtime path used by
`sloppy run --artifacts <dir>`.

Run defaults and options:

- Default host/port: `127.0.0.1:5173`.
- `--environment` is valid only for source-input flows (`<source>` or `sloppy.json`).
- `--stdlib` overrides bootstrap stdlib root.
- `--once METHOD TARGET` sends one request and prints an HTTP response.
- `--once` target must start with `/`.
- `--once` parser accepts `GET`, `POST`, `PUT`, `PATCH`, `DELETE`, `OPTIONS`, `HEAD`.

Runtime gates:

- `sloppy run` requires a V8-enabled build for handler execution.
- Plan target must match runtime expectations (`target.engine = v8`, current accepted target platform emitted by compiler is `windows-x64`).

### Plan inspection commands

| Command | Requires `--plan` | Supports `--format` |
| --- | --- | --- |
| `routes` | yes | yes (`text`/`json`) |
| `capabilities` | yes | yes (`text`/`json`) |
| `doctor` | no | yes (`text`/`json`) |
| `audit` | yes | yes (`text`/`json`) |
| `openapi` | yes | accepted by shared parser, output remains OpenAPI JSON (`--output` path optional) |

`doctor`/`audit` status values are `ok`, `warn`, `error`, and `note` in current golden outputs.

## sloppyc

```text
sloppyc --help
sloppyc --version
sloppyc build <input.js|input.ts> --out <directory> [--environment <name>] [--host <host>] [--port <port>] [--config-dir <dir>] [--config <key=value>]
```

Current parser behavior:

- Exactly one input file is required.
- `--out <directory>` is required.
- Input extension support is `.js`, `.mjs`, `.ts` (the help text still prints `input.js|input.ts`).
- `--config` can be repeated and must be `KEY=VALUE` with non-empty `KEY`.
- `--port` accepts `0..65535` in compiler CLI parsing.

Compiler output:

```text
<out>/
  app.plan.json
  app.js
  app.js.map
```

## Common CLI diagnostics

- Unknown top-level command: `sloppy: unknown command '<name>'`.
- Unknown option: `sloppy: unknown option '<flag>'`.
- Unsupported source extension: `sloppy run: unsupported source input: <path>` or `sloppy build: unsupported source input: <path>`.
- Missing required plan argument: `<command>: --plan <path> is required`.

## Boundaries

The CLI does not include a package manager or npm-style dependency resolution.
Source input is limited to the documented Sloppy compiler subset.
