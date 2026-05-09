# How To Run An App

Run a Sloppy app as a local development server.

## Prerequisites

- A built artifact directory (`app.plan.json`, `app.js`, `app.js.map`) or a source entry file.
- A V8-enabled Sloppy runtime build for handler execution.

## Steps

1. Run from existing artifacts.

```powershell
sloppy run --artifacts .sloppy
```

2. Or compile source input and run it.

```powershell
sloppy run src/main.ts
```

3. Optional host/port override.

```powershell
sloppy run --artifacts .sloppy --host 127.0.0.1 --port 8080
```

## Expected Result

The server prints:

```text
Sloppy dev server listening on http://127.0.0.1:5173
Bounded development server: HTTP/1.1, local development only.
```

When source input is used, artifacts are emitted first (default `.sloppy/cache/dev/source-input`).

## Common Failures

- `sloppy run: sloppy run requires V8-enabled build`.
- `sloppy run: expected source input, sloppy.json, or --artifacts <dir>`.
- `sloppy run: unsupported source input`: only `.js`, `.mjs`, `.ts`.
- `sloppy run: --environment only applies to source input or sloppy.json`.
- `sloppy run: --host must be an IPv4 address`.
