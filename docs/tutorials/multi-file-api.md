# Build A Multi-File API

Use the `api` template when you want route modules, configuration, services,
SQLite provider metadata, and packaging flow. It is a public alpha starter:
use it for early apps and experiments, not as a stable API contract.

## Create

```sh
sloppy create team-api --template api
cd team-api
```

Expected result: `src/main.ts` imports route modules from `src/routes/`.

## Build

```sh
sloppy build
sloppy routes .sloppy --format json
```

Expected result: JSON route metadata is printed for the health and users
routes.

## Run Health

```sh
sloppy run .sloppy --once GET /health
```

Expected body:

```text
ok
```

Supported npm platform packages include handler execution support. If a
source-built CLI was configured without that support, the command fails before
entering handlers with a clear diagnostic.

## Package

```sh
sloppy package --format json
```

Expected result: `.sloppy/package/manifest.json` and copied artifacts are
created. Re-running `sloppy package` cleans stale files in `.sloppy/package`
before writing the new package.
