# Quickstart

Three short flows that show what Sloppy does end-to-end. Each one runs from a
fresh `sloppy create` and finishes with a packaged app you can run from outside
the source checkout.

Prerequisites:

- `sloppy` is on your `PATH` ([Install](install.md)).
- `sloppy --version` succeeds. `sloppy doctor` reports what the install can do.
  Steps that execute handlers need a V8-enabled runtime; `sloppy build`,
  `routes`, `capabilities`, `doctor`, `audit`, `openapi`, and `package` only
  read metadata.

Pre-alpha note: APIs and artifact formats may change between alpha revisions.

## API

The `api` template is the recommended backend starter. It is SQLite-backed,
ships routes/services/config/repository structure, and exercises the full
build → inspect → run → package loop.

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

What happens:

- `sloppy create my-api` copies the default `api` template (no `--template`
  flag needed).
- `sloppy build` runs the compiler and writes `.sloppy/app.plan.json`,
  `.sloppy/app.js`, and `.sloppy/app.js.map`.
- `sloppy routes`, `capabilities`, and `doctor` read Plan metadata; they do
  not need a V8 runtime.
- `sloppy run .sloppy --once GET ...` runs one synthetic request through the
  runtime and prints the response body. Drop `--once` to start a real HTTP
  server on `127.0.0.1:5173`.
- `sloppy package` validates the artifacts and writes `.sloppy/package/` with
  a manifest, copied artifacts, and (when present) the bundled dependency
  graph.
- `sloppy run .sloppy/package --once ...` runs the packaged app.

For a smaller starting point, use `--template minimal-api` for a two-route app
with no SQLite or services.

## Program Mode (CLI)

Program Mode is the route-free shape for console tools and small local
programs. The `cli` template is a practical CLI starter with subcommands and
exit codes.

```sh
sloppy create my-cli --template cli
cd my-cli
sloppy run src/main.ts -- --help
sloppy build
sloppy run .sloppy -- --help
sloppy package
sloppy run .sloppy/package -- --help
```

What happens:

- `sloppy run src/main.ts -- --help` compiles the source on the fly and runs
  the entrypoint. Everything after `--` is forwarded to `main(args, ctx)`.
- `sloppy build` writes a `kind: "program"` Plan plus the generated bundle.
- `sloppy run .sloppy -- ...` runs the built artifacts.
- `sloppy package` writes the same `.sloppy/package/` layout with a Program
  manifest.

The Program entrypoint can be `export async function main(args, ctx)`, a
default exported function `(args, ctx)`, or top-level module execution. Numeric
returns from `main` set the process exit code; values out of `0..255` exit
non-zero with a diagnostic.

For the smallest Program starter, use `--template program` instead.

## Packages: a backend that consumes a local pure-JS package

The `package-api` template is a backend that uses an installed pure-JavaScript
package through an npm `file:` dependency. It is the smallest end-to-end demo
of Sloppy's experimental package support.

```sh
sloppy create my-package-api --template package-api
cd my-package-api
npm install --ignore-scripts --no-audit
sloppy build
sloppy deps .sloppy
sloppy package
sloppy run .sloppy/package --once GET /health
```

What happens:

- `npm install --ignore-scripts --no-audit` installs the local pure-JS package
  into `node_modules` without running install scripts. Sloppy reads installed
  packages at build time; it does not install for you.
- `sloppy build` resolves the package, transforms the supported JavaScript,
  bundles it into `app.js`, and records the dependency graph in
  `app.plan.json` and `.sloppy/deps.graph.json`.
- `sloppy deps .sloppy` prints the bundled packages, modules, assets, and
  Node compatibility shims used by the build.
- `sloppy package` copies the artifacts and the dependency graph into
  `.sloppy/package/`. The packaged app runs without `node_modules` at the
  package location.

Sloppy does not install dependencies, solve semver ranges, or read lockfiles.
It does not support Node native addons or N-API. Unsupported package or export
shapes fail clearly during build. See
[Using installed packages](guide/using-packages.md) and
[Node compatibility](reference/node-compatibility.md).

## What's next

- [Templates](guide/templates.md) — every starter, what it covers, and where
  to edit next.
- [Project layout](guide/project-layout.md) — `sloppy.json`, `appsettings.json`,
  source structure.
- [Tutorials](tutorials/index.md) — guided walkthroughs that build on these
  flows.
- [API: app](api/app.md), [routing](api/routing.md), [results](api/results.md).
- [CLI](cli/index.md) — every flag for `create`, `build`, `run`, `package`,
  `routes`, `deps`, `capabilities`, `doctor`, `audit`, and `openapi`.
- [Stability](reference/stability.md) — what is supported, experimental, or
  rejected by the current compiler/runtime.
