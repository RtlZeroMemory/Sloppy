# Generate OpenAPI And Package The App

Use this after `sloppy build` succeeds.

## Build

```sh
sloppy build
```

Expected result: `.sloppy/app.plan.json` exists.

## Generate OpenAPI

```sh
sloppy openapi .sloppy --output openapi.json
```

Expected result: `openapi.json` contains the Plan-derived route document.
OpenAPI output is intentionally limited to metadata the compiler emits today.

## Package

```sh
sloppy package --format json
```

Expected result:

```text
.sloppy/package/
  manifest.json
  artifacts/
    app.plan.json
    app.js
    app.js.map
```

The package command validates artifacts and removes stale output before writing
new package contents.

## Smoke The Packaged Artifacts

```sh
sloppy doctor .sloppy
sloppy audit .sloppy
sloppy routes .sloppy
```

Expected result: each command reads the generated Plan without entering V8.
