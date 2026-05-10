# `sloppy package`

Compile source input and write a directory app package under `.sloppy/package/`
by default, or under `<out>/package/` for explicit source output.

```sh
sloppy package [source.js|source.mjs|source.ts] [--out <dir>]
               [--environment <name>] [--host <ip>] [--port <n>]
               [--kind web|program]
               [--format text|json]
```

This command packages a Sloppy app's generated artifacts. It is not the same as
a Sloppy runtime release archive.

## Modes

**Project mode** (no positional argument):

```sh
sloppy package
```

Reads `sloppy.json`, compiles `entry` into `outDir`, validates the generated
artifacts, then writes `<outDir>/package/`.

**Explicit source**:

```sh
sloppy package src/main.ts
```

Compiles the supplied source and writes `.sloppy/package/`. Use `--out dist`
when you need a separate artifact root.

## Output

```text
<out>/package/
  manifest.json
  artifacts/
    app.plan.json
    app.js
    app.js.map
    deps.graph.json   optional, when dependency graph metadata exists
```

`manifest.json` currently uses `schema: "sloppy.app-package.v1"`. The package
records the app kind (`"web"` or `"program"`) and copied artifact paths:

```json
{
  "schema": "sloppy.app-package.v1",
  "kind": "program",
  "dependencyMode": "bundled",
  "entry": "artifacts/app.plan.json",
  "artifacts": {
    "plan": "artifacts/app.plan.json",
    "bundle": "artifacts/app.js",
    "sourceMap": "artifacts/app.js.map",
    "dependencyGraph": "artifacts/deps.graph.json"
  },
  "createdBy": "sloppy package"
}
```

The package is a directory shape for local tooling and smoke tests; archive,
signing, and runtime release packaging are handled by the release scripts under
`tools/`.

`dependencyMode: "bundled"` means compatible dependency modules have been
emitted into the generated artifacts. Packaged apps do not read the original
source checkout or `node_modules` at run time for bundled modules.

## Flags

| Flag | Default | Purpose |
| --- | --- | --- |
| `--out <dir>` | `outDir` from `sloppy.json`, or `.sloppy` for explicit source | Artifact output directory before packaging |
| `--environment <name>` | `Development` | Selects `appsettings.{Environment}.json` overlay |
| `--host <ip>` | `127.0.0.1` | Server host baked into the Plan |
| `--port <n>` | `5173` | Server port baked into the Plan |
| `--kind web\|program` | inferred for direct source, `web` for project mode without `kind` | Override source kind |
| `--format json` / `--json` | text | Print structured success output |

Project mode owns `outDir` through `sloppy.json`; `sloppy package --out ...`
without a positional source is rejected so the project contract stays explicit.

## Examples

```sh
# Package the current project.
sloppy package

# Package a one-off source file.
sloppy package src/main.ts

# Package a route-free program source.
sloppy package src/main.ts --kind program

# Run a packaged program.
sloppy run .sloppy/package -- one two

# Machine-readable result.
sloppy package --format json
```

Inspect the packaged Plan with other CLI commands:

```sh
sloppy routes --plan .sloppy/package/artifacts/app.plan.json
sloppy openapi .sloppy/package/artifacts --output openapi.json
```

`sloppy openapi` is web-only; it fails clearly when the packaged Plan is
`kind: "program"`.
