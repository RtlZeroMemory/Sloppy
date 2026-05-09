# Sloppy Dogfood API

This template mirrors the shape of a small control-plane service: projects,
apps, builds, deployments, diagnostics, and health routes. The data is static
so the template can compile and run without a database.

## Run

```powershell
sloppy build
sloppy package
sloppy run --once GET /health
```

The richer SQLite-backed dogfood example lives in `examples/prealpha-control-plane`.
