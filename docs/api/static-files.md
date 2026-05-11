# Static Files

`app.useStaticFiles(options)` exposes a project directory as static `GET`
routes in the current alpha compiler.

```ts
const app = Sloppy.create();

app.useStaticFiles({
    requestPath: "/public",
    root: "public",
    cache: { maxAgeSeconds: 3600 },
});
```

## Options

| Option | Type | Required | Behavior |
| --- | --- | --- | --- |
| `requestPath` | string | yes | Absolute route prefix such as `/public`. It must not contain route parameters. |
| `root` | string | yes | Project-relative directory to include. Absolute paths and traversal are rejected. |
| `cache.maxAgeSeconds` | integer | no | Adds `Cache-Control: public, max-age=<seconds>` to generated static responses. |

## What Gets Served

The compiler snapshots supported files under `root` and emits one `GET` route
per file. Supported extensions are:

- `.txt`
- `.json`
- `.css`
- `.js`
- `.mjs`
- `.svg`
- `.png`
- `.jpg`
- `.jpeg`
- `.wasm`

Each generated response includes a content type and an `ETag` derived from the
file bytes. Static routes are also recorded in `deps.graph.json` as assets.
Each asset must be 1 MiB or smaller in the current alpha because generated
handlers inline the bytes.

## Alpha Behavior

Static files are captured when you build or package the app. Sloppy does not
browse the directory at request time, does not serve newly-created files until
you rebuild, and does not expose directory listings.

`sloppy package` copies recorded assets into `artifacts/assets/` so packages
carry the source assets they were built from. The generated handlers currently
embed the file bytes too, which keeps `sloppy run .sloppy/package --once ...`
working outside the original checkout.
