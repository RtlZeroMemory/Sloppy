# Serve Static Assets

Alpha: `app.useStaticFiles` is an experimental build-time API.

Use `app.useStaticFiles` when an app needs to ship fixed files such as CSS,
client JavaScript, SVGs, images, or plain text.

## Add A Public Directory

Create a project-relative directory:

```text
public/
  app.js
  site.css
  logo.svg
```

Register it from the app entrypoint:

```ts
import { Sloppy } from "sloppy";

const app = Sloppy.create();

app.useStaticFiles({
    requestPath: "/public",
    root: "public",
    cache: { maxAgeSeconds: 3600 },
});

export default app;
```

## Build And Check Routes

```sh
sloppy build
sloppy routes .sloppy
sloppy run .sloppy --once GET /public/app.js
```

The `--once` response includes the same HTTP status line, headers, and body
that the development transport serializer writes.

## Package The App

```sh
sloppy package
sloppy run .sloppy/package --once GET /public/app.js
```

The package includes copied static assets under `artifacts/assets/`. Rebuild
and repackage after changing files in `public/`.

## Limits

- Static assets are a build-time snapshot.
- Only the documented extensions are served.
- Each asset must be 1 MiB or smaller in the current alpha.
- Directory listings, range requests, compression negotiation, upload handling,
  and runtime file watching are not implemented.
