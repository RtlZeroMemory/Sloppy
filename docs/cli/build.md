# `sloppy build`

Compile source input into Plan-backed artifacts. `build` never enters V8 —
it produces files and exits.

```
sloppy build [source] [--out <dir>]
             [--environment <name>] [--host <ip>] [--port <n>]
             [--kind web|program]
```

## Modes

**Project mode** (no positional argument):

```
sloppy build
```

Reads `sloppy.json` from the current directory, compiles `entry`, writes to
`outDir`.

**Explicit source**:

```
sloppy build src/main.ts
sloppy build src/main.ts --out dist
```

Compiles the supplied file. Without `--out`, output lands in the
`.sloppy` artifact directory.

## Flags

| Flag                  | Default          | Purpose                                                  |
| --------------------- | ---------------- | -------------------------------------------------------- |
| `--out <dir>`         | `outDir` from `sloppy.json`, or `.sloppy` for explicit source | Artifact output directory |
| `--environment <name>`| `Development`    | Selects `appsettings.{Environment}.json` overlay          |
| `--host <ip>`         | `127.0.0.1`      | Server host baked into the Plan                          |
| `--port <n>`          | `5173`           | Server port baked into the Plan                          |
| `--kind web\|program` | inferred for direct source, `web` for project mode without `kind` | Override source kind |

`--artifacts` is **not** accepted for `build`. Use `run --artifacts` to load
an already-built Plan.

## Output

```
<out>/
  app.plan.json   the application Plan (deterministic JSON)
  app.js          the generated runtime bundle
  app.js.map      the source map
  deps.graph.json optional dependency graph artifact
```

`app.plan.json` is what every other command (`routes`, `audit`,
`openapi`, …) reads. It is deterministic for the same source tree, config
directory, environment overlay, environment-derived config values, CLI
overrides, and compiler version. Hashing those inputs plus the Plan is fine
for cache keys.

Don't check generated artifacts (`.sloppy/`) into source control by
default; they regenerate from source. Intentional fixtures and test
goldens are the only normal place to commit Plan output.

When source imports installed packages, Node compatibility shims, or
`moduleInclude`/`assetInclude` entries, the Plan also carries dependency graph
metadata. Inspect it with:

```sh
sloppy deps .sloppy
```

## Examples

```
# Standard project build.
sloppy build

# Build a one-off source file into ./.sloppy.
sloppy build examples/compiler-hello/app.js

# Build a route-free program.
sloppy build examples/program-hello/main.ts

# Build with a different environment overlay.
sloppy build --environment Production
```

## Failures

If compilation fails, `build` prints the diagnostic and exits with status 1
without writing artifacts. The diagnostic includes the source location and
a stable diagnostic code.

For supported syntax limits, see
[reference/supported-syntax.md](../reference/supported-syntax.md).
