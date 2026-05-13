# Static Files Precompressed

Shows precompressed variant registration. Requests that allow `br` or `gzip`
can receive `app.js.br` or `app.js.gz` when the files exist.

```powershell
sloppy build examples/static-files-precompressed/app.js --out .sloppy-static-precompressed
sloppy run .sloppy-static-precompressed --header "accept-encoding: br" --once GET /assets/app.js
```

The `.br` and `.gz` files are real compressed variants of `public/app.js`.
Regenerate them from the source asset whenever the JavaScript changes.
