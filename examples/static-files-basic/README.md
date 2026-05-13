# Static Files Basic

Serves a project-local `public/` directory through `app.staticFiles`.

```powershell
sloppy build examples/static-files-basic/app.js --out .sloppy-static-basic
sloppy run .sloppy-static-basic --once GET /assets/app.js
```

Static assets are captured at build time. Rebuild after changing files under
`public/`.
