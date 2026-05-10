# `sloppyc`

`sloppyc` is the standalone Sloppy compiler. Most users should call
`sloppy build` or `sloppy run`; those commands invoke `sloppyc` with the right
paths for the current project.

Use `sloppyc` directly when you are testing compiler behavior or building a
lower-level tool around emitted artifacts.

```sh
sloppyc --help
sloppyc --version
sloppyc build <input.js|input.ts> --out <directory>
```

## Build

```sh
sloppyc build src/main.ts --out .sloppy
```

Output:

```text
.sloppy/
  app.plan.json
  app.js
  app.js.map
```

## Flags

| Flag | Purpose |
| --- | --- |
| `--out <directory>` | Required artifact output directory |
| `--environment <name>` | Select environment-specific config |
| `--host <host>` | Emit server host metadata into the Plan |
| `--port <port>` | Emit server port metadata into the Plan |
| `--config-dir <dir>` | Read config from a directory other than the input root |
| `--config <key=value>` | Apply a config override |
| `--timings-json <file>` | Write compiler phase timing and counter JSON |
| `--diagnostics-timing-json <file>` | Alias for `--timings-json` |

`--timings-json` is contributor measurement output. It is useful for comparing
compiler changes; it is not a public performance guarantee.

## Exit codes

| Code | Meaning |
| --- | --- |
| `0` | Build succeeded |
| `1` | Compilation failed with a diagnostic |
| `2` | CLI usage error |

Diagnostics print to stderr and include stable diagnostic codes where the
compiler has one.
