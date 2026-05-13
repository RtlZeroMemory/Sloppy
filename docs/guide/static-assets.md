# Serve Static Assets

Use `app.staticFiles` when an app needs to ship fixed files such as CSS,
client JavaScript, SVGs, images, WebAssembly modules, or plain text. Use
`app.spa` when the same app should serve a browser client and fall back to
`index.html` for client-side routes.

## Add A Public Directory

Create a project-relative directory:

```text
public/
  app.js
  app.js.br
  app.js.gz
  site.css
  logo.svg
```

Register it from the app entrypoint:

```ts
import { Sloppy } from "sloppy";

const app = Sloppy.create();

app.staticFiles("/assets", {
    root: "public",
    cacheControl: "public, max-age=31536000, immutable",
    precompressed: true,
});

export default app;
```

`GET /assets/app.js` serves `public/app.js`. Requests with
`Accept-Encoding: br` or `Accept-Encoding: gzip` use the matching sibling file
when it exists.

## Add An SPA Fallback

For a built client app:

```text
dist/
  index.html
  assets/app.js
```

Register the SPA:

```ts
app.spa("/", {
    root: "dist",
    fallback: "index.html",
    cacheControl: {
        html: "no-cache",
        assets: "public, max-age=31536000, immutable",
    },
});
```

`GET /dashboard` returns `dist/index.html`. `GET /assets/app.js` serves the
asset. A missing extension-looking path such as `/missing.js` returns `404`.

## Build And Check Routes

```sh
sloppy build
sloppy routes .sloppy
sloppy run .sloppy --once GET /assets/app.js
```

The `--once` response includes the same status line, headers, and body that the
development transport serializer writes. Static responses support `HEAD`,
conditional requests, single byte ranges, cache headers, and precompressed
variant negotiation.

## Package The App

```sh
sloppy package
sloppy run .sloppy/package --once GET /assets/app.js
```

The package includes copied static assets under `artifacts/assets/`. Rebuild
and repackage after changing files in `public/` or `dist/`.

## Limits

- Static assets are a build-time snapshot.
- The compiler serves the documented extensions and records matching
  precompressed siblings when enabled.
- Compiler-generated handlers inline file bytes and reject files over the
  configured `maxFileBytes` limit.
- Directory listings, uploads, and runtime file watching are not implemented.
