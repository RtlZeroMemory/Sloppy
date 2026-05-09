# `sloppy build`

Compile source input into Plan-backed artifacts. `build` never enters V8 —
it produces files and exits.

```
sloppy build [source] [--out <dir>]
             [--environment <name>] [--host <ip>] [--port <n>]
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
deterministic source-input cache directory inside the project's `.sloppy`.

## Flags

| Flag                  | Default          | Purpose                                                  |
| --------------------- | ---------------- | -------------------------------------------------------- |
| `--out <dir>`         | `outDir` from `sloppy.json`, or a cache subdir | Artifact output directory |
| `--environment <name>`| `Development`    | Selects `appsettings.{Environment}.json` overlay          |
| `--host <ip>`         | `127.0.0.1`      | Server host baked into the Plan                          |
| `--port <n>`          | `5173`           | Server port baked into the Plan                          |

`--artifacts` is **not** accepted for `build`. Use `run --artifacts` to load
an already-built Plan.

## Output

```
<out>/
  app.plan.json   the application Plan (deterministic JSON)
  app.js          the generated runtime bundle
  app.js.map      the source map
```

`app.plan.json` is what every other command (`routes`, `audit`, `openapi`,
…) reads. It's fully deterministic given the same source — checking it in
or hashing it for cache keys is fine.

## Examples

```
# Standard project build.
sloppy build

# Build a one-off file into ./dist.
sloppy build examples/hello/app.ts --out dist

# Build with a different environment overlay.
sloppy build --environment Production
```

## Failures

If compilation fails, `build` prints the diagnostic and exits with status 1
without writing artifacts. The diagnostic includes the source location and
a stable diagnostic code.

For supported syntax limits, see
[reference/supported-syntax.md](../reference/supported-syntax.md).
