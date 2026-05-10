# Build A Multi-File API

Use the `full-api` template when you want route modules without adding external
dependencies.

## Create

```sh
sloppy create team-api --template full-api
cd team-api
```

Expected result: `src/main.ts` imports route modules from `src/routes/`.

## Build

```sh
sloppy build
sloppy routes .sloppy --format json
```

Expected result: JSON route metadata is printed for the health, users, and
projects routes.

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
