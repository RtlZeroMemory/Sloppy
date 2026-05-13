# Static Files Package

Shows the package path for static assets. `sloppy package` copies dependency
graph assets into `artifacts/assets/` so the package carries the static files.

```powershell
sloppy package examples/static-files-package/app.js --out .sloppy-static-package
sloppy run .sloppy-static-package/package --once GET /public/index.html
```

The package is still an alpha local app package, not a production release
archive.
