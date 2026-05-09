# Full Sloppy API

This template keeps the app split across route modules. It is still dependency
free and compiles through `sloppy build`.

## Run

```powershell
sloppy build
sloppy package
sloppy run --once GET /health
```

`sloppy package` writes a local app package under `.sloppy/package`.
