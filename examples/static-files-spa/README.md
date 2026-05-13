# Static Files SPA

Hosts a built browser app with `app.spa`. Concrete API routes still win before
the SPA fallback.

```powershell
sloppy build examples/static-files-spa/app.js --out .sloppy-static-spa
sloppy run .sloppy-static-spa --once GET /dashboard
```

`/dashboard` returns `dist/index.html`. Missing asset-looking paths such as
`/missing.js` return `404`.
