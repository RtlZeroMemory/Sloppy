# Minimal API Template

The smallest public alpha API starter. Two routes, no services, no SQLite.
Use it for a quick first run or a smoke test; use [`api`](../api/README.md)
when you want a fuller backend layout.

Pre-alpha note: APIs and artifact formats may change between alpha revisions.

## Build, run, package

```sh
sloppy build
sloppy routes .sloppy
sloppy run .sloppy --once GET /health
sloppy run .sloppy --once GET /hello/Ada
sloppy package
sloppy run .sloppy/package --once GET /health
```

Expected responses:

- `GET /health` → `ok`
- `GET /hello/Ada` → `{"hello":"Ada"}`

## Where to edit next

- Add routes directly in `src/main.ts`.
- Graduate to the [`api`](../api/README.md) template when you need services,
  config, or data.

## Current limitations

- Pre-alpha. APIs and artifact formats may change between alpha revisions.
- No services, configuration overlay, or data provider. Add them as needed
  or switch to the `api` template.
