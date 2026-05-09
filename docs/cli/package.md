# `sloppy package`

Compile source input and write a directory app package under the artifact
output directory.

```
sloppy package [source.js|source.mjs|source.ts] [--out <dir>]
               [--environment <name>] [--host <ip>] [--port <n>]
               [--format text|json]
```

This command packages a Sloppy app's generated artifacts. It is not the same as
a Sloppy runtime release archive.

## Modes

**Project mode** (no positional argument):

```
sloppy package
```

Reads `sloppy.json`, compiles `entry` into `outDir`, validates the generated
artifacts, then writes `<outDir>/package/`.

**Explicit source**:

```
sloppy package src/main.ts --out dist
```

Compiles the supplied source and writes `dist/package/`.

## Output

```
<out>/package/
  manifest.json
  artifacts/
    app.plan.json
    app.js
    app.js.map
```

`manifest.json` currently uses `schema: "sloppy.app-package.v1"`. The package
is a directory shape for local tooling and smoke tests; archive/signing/release
packaging is handled by the release scripts under `tools/`.

## Flags

| Flag | Default | Purpose |
| --- | --- | --- |
| `--out <dir>` | `outDir` from `sloppy.json`, or `.sloppy` for explicit source | Artifact output directory before packaging |
| `--environment <name>` | `Development` | Selects `appsettings.{Environment}.json` overlay |
| `--host <ip>` | `127.0.0.1` | Server host baked into the Plan |
| `--port <n>` | `5173` | Server port baked into the Plan |
| `--format json` / `--json` | text | Print structured success output |

Project mode owns `outDir` through `sloppy.json`; `sloppy package --out ...`
without a positional source is rejected so the project contract stays explicit.

## Examples

```
# Package the current project.
sloppy package

# Package a one-off source file.
sloppy package src/main.ts --out dist

# Machine-readable result.
sloppy package --format json
```

Inspect the packaged Plan with other CLI commands:

```
sloppy routes --plan .sloppy/package/artifacts/app.plan.json
sloppy openapi --artifacts .sloppy/package/artifacts --output openapi.json
```
