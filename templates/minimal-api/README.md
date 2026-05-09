# Minimal Sloppy API

This template is the smallest project layout that `sloppy create` ships.

## Run

```powershell
sloppy build
sloppy run --once GET /health
```

`sloppy run` requires a V8-enabled runtime build. `sloppy build` verifies the
compiler and artifact path without entering V8.
