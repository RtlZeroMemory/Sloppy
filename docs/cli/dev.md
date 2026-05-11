# `sloppy dev` (experimental)

Build a web project, start the local development server, and rebuild on
project file changes. This command is experimental; behavior and support may
change during the public alpha, pre-production period.

```text
sloppy dev [source]
           [--host <ip>] [--port <n>]
           [--watch|--no-watch] [--clear] [--openapi]
           [--env <name>] [--environment <name>]
```

## Modes

**Project mode**:

```sh
sloppy dev
```

Reads `sloppy.json`, compiles `entry` into `outDir`, starts the HTTP server,
and watches project inputs.

**Explicit source**:

```sh
sloppy dev src/main.ts
```

Compiles the supplied source into `.sloppy`, starts the HTTP server, and watches
the source directory plus common project inputs.

## Flags

| Flag | Default | Purpose |
| --- | --- | --- |
| `--host <ip>` | `127.0.0.1` or Plan config | Server bind host |
| `--port <n>` | `5173` or Plan config | Server bind port |
| `--watch` | on | Rebuild after watched file changes |
| `--no-watch` | off | Build and run the dev server without reloads |
| `--clear` | off | Clear the terminal before the initial build and reload output |
| `--openapi` | off | Write `<outDir>/openapi.json` after successful builds when the Plan is web/OpenAPI-compatible |
| `--env <name>` | `Development` or `sloppy.json` environment | Short alias for `--environment` |
| `--environment <name>` | `Development` or `sloppy.json` environment | Select appsettings overlay |

`sloppy dev` (experimental) requires a web Plan. Program Mode projects should use
`sloppy run`.

## Watched Inputs

The development loop uses portable polling. It watches:

- the project source directory, or the explicit source file's containing directory
- `sloppy.json`
- `appsettings.json`
- `appsettings.Development.json`
- `public/`, `static/`, and `wwwroot/`
- `templates/` and `views/`
- `assetInclude` and `moduleInclude` roots from `sloppy.json`
- migration directories configured in `sloppy.json`

Changes are debounced before rebuild. The changed file paths are printed before
the rebuild starts.

## Reload Behavior

Startup does an initial build, loads the generated artifacts, and starts the
same development HTTP transport used by `sloppy run`.

On a watched change:

1. `sloppy dev` (experimental) rebuilds the source.
2. If the build fails, the previous server keeps running.
3. If the build succeeds, `sloppy dev` (experimental) stops the old server and starts the new
   artifacts.
4. If `--openapi` is set, OpenAPI is refreshed after each successful rebuild
   when the Plan supports it.

Build diagnostics are printed without native stack dumps. `Ctrl+C` stops the
server, shuts down the app lifecycle, and exits.

## Examples

```sh
# Build, run, and reload the current project.
sloppy dev

# Explicit watch mode, custom port.
sloppy dev --watch --port 8080

# Rebuild and refresh OpenAPI on changes.
sloppy dev --openapi

# Run without reloads.
sloppy dev --no-watch

# Use a non-default environment overlay.
sloppy dev --env Staging
```
