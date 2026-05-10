# Templates

`sloppy create <name>` copies a built-in starter into a new directory. The
public alpha ships six templates covering both Web Mode (HTTP APIs) and
Program Mode (CLIs and tools).

```sh
sloppy create my-app                          # default: api
sloppy create my-app --template <name>
```

| Template | Mode | Best for |
| --- | --- | --- |
| [`api`](#api) | Web | Recommended backend starter; SQLite-backed |
| [`minimal-api`](#minimal-api) | Web | Smallest API for first runs and smoke tests |
| [`program`](#program) | Program | Smallest Program Mode starter |
| [`cli`](#cli) | Program | Practical CLI-style Program Mode starter |
| [`package-api`](#package-api) | Web | Backend that consumes a local pure-JS package |
| [`node-compat`](#node-compat) | Program | Program Mode demo of supported Node shims |

All templates are pre-alpha. Their structure and supported runtime surface
can change before a stable release.

## api

The recommended public alpha starter. Routes, route modules, a small
service/repository split, configuration, SQLite provider metadata, health
endpoints, and the app package flow.

```sh
sloppy create my-api
cd my-api
sloppy build
sloppy routes .sloppy
sloppy capabilities .sloppy
sloppy doctor .sloppy
sloppy run .sloppy --once GET /health
sloppy run .sloppy --once GET /users
sloppy package
sloppy run .sloppy/package --once GET /health
```

Layout:

- `src/main.ts` wires the app, SQLite provider, middleware, services, and
  routes.
- `src/routes/` contains route registration modules.
- `src/services/` contains business logic.
- `src/db/` contains the SQLite migration and repository helpers.
- `src/models/` contains request and response shapes.
- `appsettings*.json` contains runtime configuration.
- `data/` is where the development SQLite file is created.

Where to edit next: add a route module under `src/routes/`, register it in
`src/main.ts`, and add a service under `src/services/` if it needs shared
state. Configure providers in `appsettings.Development.json`.

Limits: SQLite is the strongest provider path. PostgreSQL and SQL Server
templates are not part of the alpha; configure them on top of the `api`
template if you have a live database.

## minimal-api

Two routes, no services, no SQLite. Use it for a quick first run or a smoke
test.

```sh
sloppy create my-api --template minimal-api
cd my-api
sloppy build
sloppy routes .sloppy
sloppy run .sloppy --once GET /health
sloppy run .sloppy --once GET /hello/Ada
sloppy package
sloppy run .sloppy/package --once GET /health
```

Where to edit next: add routes directly in `src/main.ts`, then graduate to
the `api` template when you need services, config, and data.

## program

The smallest route-free Program Mode starter for console tools, local jobs,
and scripts.

```sh
sloppy create my-tool --template program
cd my-tool
sloppy run src/main.ts -- --name Ada
sloppy build
sloppy run .sloppy -- --name Ada
sloppy package
sloppy run .sloppy/package -- --name Ada
```

Where to edit next: edit `src/main.ts`. The entrypoint is
`export async function main(args, ctx)`; numeric returns set the exit code.

Limits: Program Mode does not provide full Node globals, native addons, or
raw terminal APIs. Use the Sloppy stdlib (`sloppy/fs`, `sloppy/os`,
`sloppy/time`, etc.) or supported `node:*` shims.

## cli

A practical CLI starter shaped like a real local tool. Command dispatcher,
subcommands, filesystem inspection, safe platform indicators, clear exit
codes, and the packaged CLI flow.

```sh
sloppy create my-cli --template cli
cd my-cli
sloppy run src/main.ts -- --help
sloppy run src/main.ts -- echo hello
sloppy run src/main.ts -- inspect package.json
sloppy build
sloppy run .sloppy -- --help
sloppy package
sloppy run .sloppy/package -- echo hello
```

Where to edit next: add a command module under `src/commands/` and dispatch
it from `src/main.ts`. Keep argument parsing in one place so error messages
stay consistent.

## package-api

A backend that uses a compatible local pure-JS package through an npm `file:`
dependency. It does not require internet access.

```sh
sloppy create my-package-api --template package-api
cd my-package-api
npm install --ignore-scripts --no-audit
sloppy build
sloppy deps .sloppy
sloppy deps .sloppy --format json
sloppy run .sloppy --once GET /health
sloppy run .sloppy --once GET /users/Ada
sloppy package
sloppy run .sloppy/package --once GET /health
sloppy run .sloppy/package --once GET /users/Ada
```

Sloppy bundles the installed package into the generated artifacts and records
the graph for `sloppy deps`. The packaged app contains the bundled dependency
graph and runs without `node_modules` at the package location.

Where to edit next: install another compatible pure-JS package with your
package manager and import it from `src/`. Inspect what was bundled with
`sloppy deps`.

Limits: package support is experimental. Sloppy does not install from a
registry, solve semver ranges, or read lockfiles. Native addons and N-API are
unsupported. See [Using installed packages](using-packages.md).

## node-compat

A Program Mode starter that exercises supported Node compatibility shims.

```sh
sloppy create my-node-tool --template node-compat
cd my-node-tool
sloppy build
sloppy deps .sloppy
sloppy run .sloppy
sloppy package
sloppy run .sloppy/package
```

The template uses `node:path`, `node:events`, `node:buffer`, and
`node:querystring`. It intentionally avoids `node:stream`, `node:http`,
`node:net`, `node:tls`, `node:child_process`, native addons, and implicit
globals such as `process` or `Buffer`.

Where to edit next: import another supported shim listed in
[Node compatibility](../reference/node-compatibility.md). Unsupported builtins
fail clearly at build time.

Limits: Node compatibility is not full Node compatibility. The shim set grows
over time through explicit slices backed by Sloppy Core APIs.
