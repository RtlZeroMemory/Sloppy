# Package API Starter

A public alpha backend starter that consumes a compatible local pure-JS
package through an npm `file:` dependency. It does not require internet
access.

Pre-alpha note: APIs and artifact formats may change between alpha revisions.
Package support is experimental — see "Current limitations" below.

## How package support works

Sloppy reads packages that are already installed in `node_modules`, bundles
the compatible pure-JavaScript modules into the generated artifacts, and
records the graph for `sloppy deps`. It does not install dependencies, solve
semver ranges, or read lockfiles. Use your normal package manager (here,
`npm install`) before running `sloppy build`.

## Build, run, package

```sh
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

The packaged app contains the bundled dependency graph and runs without
`node_modules` at the package location.

## Where to edit next

- Install another compatible pure-JS package with `npm install <name>` and
  import it from `src/`.
- Run `sloppy deps .sloppy` to inspect what was bundled and whether any
  Node compatibility shims are involved.
- Use [Node compatibility](../../docs/reference/node-compatibility.md) to
  check which `node:*` builtins your dependencies can use.

## Current limitations

- Pre-alpha. APIs and artifact formats may change between alpha revisions.
- Package support is experimental.
- No registry install, version solving, or lockfile awareness.
- No Node native addons or N-API.
- No full Node globals, streams, workers, VM, child process, inspector, or
  REPL compatibility.
- Unsupported package shapes fail clearly during `sloppy build`.
