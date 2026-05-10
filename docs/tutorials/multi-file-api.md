# Build A Multi-File API

Use the `api` template when you want route modules, configuration, services,
SQLite provider metadata, and packaging flow.

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

Handler execution requires a V8-enabled runtime. If the package is non-V8,
the command fails before entering handlers with a V8-required diagnostic.

## Package

```sh
sloppy package --format json
```

Expected result: `.sloppy/package/manifest.json` and copied artifacts are
created. Re-running `sloppy package` cleans stale files in `.sloppy/package`
before writing the new package.
