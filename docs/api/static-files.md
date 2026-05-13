# Static Files

`app.staticFiles(mount, options)` serves a public asset directory and
`app.spa(mount, options)` serves a client app with an HTML fallback. Both are
first-party app-host features that emit compiler-generated handlers and Plan
metadata. See the [stability matrix](../reference/stability.md) for the
current support boundary; this page documents the supported shape.

```ts
const app = Sloppy.create();

app.staticFiles("/assets", {
    root: "public",
    cacheControl: "public, max-age=31536000, immutable",
    precompressed: true,
});

app.spa("/", {
    root: "dist",
    fallback: "index.html",
    cacheControl: {
        html: "no-cache",
        assets: "public, max-age=31536000, immutable",
    },
});
```

`app.useStaticFiles(options)` remains as a compatibility alias for the older
literal shape with `requestPath` and `root`.

## `app.staticFiles(mount, options)`

| Option | Type | Required | Behavior |
| --- | --- | --- | --- |
| `mount` | string | yes | Absolute route prefix such as `/assets`. Route parameters are rejected. |
| `root` | string | yes | Project-relative directory to serve. Absolute paths and traversal are rejected. |
| `index` | string or `false` | no | Serves `index.html` at the directory path by default. Use `false` to disable directory index routes. |
| `dotfiles` | `"deny"`, `"ignore"`, or `"allow"` | no | Defaults to `deny`. Dotfiles return `403`, are hidden as `404`, or are served. |
| `cacheControl` | string | no | Adds `Cache-Control` to static responses. |
| `cache.maxAgeSeconds` | integer | no | Legacy shortcut that emits `public, max-age=<seconds>`. |
| `precompressed` | boolean or `("br" \| "gzip")[]` | no | Selects sibling `.br` or `.gz` files when `Accept-Encoding` allows them. |
| `etag` | boolean, `"weak"`, or `"strong"` | no | TestHost and compiler-generated handlers emit weak, content-sensitive ETags. |
| `lastModified` | boolean | no | TestHost emits `Last-Modified` from the selected file. Compiler-generated handlers omit `Last-Modified` because artifact mtimes are not a reliable version marker. |
| `range` | boolean | no | Defaults to `true`. Supports a single `Range: bytes=...` request. |
| `maxFileBytes` | integer | no | Compiler-generated handlers currently inline files and reject assets above the configured limit. |

## `app.spa(mount, options)`

| Option | Type | Required | Behavior |
| --- | --- | --- | --- |
| `mount` | string | yes | Absolute route prefix for the SPA. |
| `root` | string | yes | Project-relative directory containing the built client app. |
| `fallback` | string | no | HTML fallback file. Defaults to `index.html`. |
| `assetsPrefix` | string | no | Reserved metadata for asset prefix separation. |
| `cacheControl.html` | string | no | Cache policy for fallback HTML. |
| `cacheControl.assets` | string | no | Cache policy for files under the SPA root. |
| `precompressed` | boolean or `("br" \| "gzip")[]` | no | Uses sibling compressed variants for static files and fallback HTML. |
| `maxFileBytes` | integer | no | Same validation and serving limit as `app.staticFiles`. |

Concrete app routes win before static matching in `TestHost.create(app)`.
Missing SPA paths without a file extension fall back to the configured HTML
file. Missing extension-looking paths return `404`. Compiler-generated SPA
fallback routes currently cover the mount path plus nested paths up to the
native route parameter limit. A true arbitrary-depth catch-all is deferred
until the route artifact has a native catch-all shape.

## Served Behavior

Static responses include:

- `Content-Type` from the extension or a configured override in TestHost.
- `Content-Length`.
- `ETag`.
- `Last-Modified` in TestHost file-serving responses.
- `Cache-Control` when configured.
- `Accept-Ranges: bytes` when range support is enabled.
- `X-Content-Type-Options: nosniff`.
- `Content-Encoding` and `Vary: Accept-Encoding` for selected precompressed
  variants.

`GET` returns the body. `HEAD` uses the same metadata and suppresses the body.
`If-None-Match` can return `304`. TestHost also honors `If-Modified-Since`
when `Last-Modified` is present. Generated handlers do not emit
`Last-Modified` and do not use `If-Modified-Since`.

`Accept-Encoding` honors `q=0`, prefers the higher quality value, and uses the
server order for ties. Byte ranges apply only to identity bytes. Brotli and
gzip variants return `Accept-Ranges: none` and ignore `Range` rather than
slicing encoded payloads.

## Build And Package Behavior

The compiler snapshots supported files under `root` and emits generated static
handlers plus dependency graph asset records. In this alpha implementation,
generated handlers still inline bounded file bytes in JavaScript; the asset
records make package artifacts self-contained but are not native file-send
metadata yet. Supported extensions are `.txt`,
`.html`, `.json`, `.css`, `.js`, `.mjs`, `.svg`, `.png`, `.jpg`, `.jpeg`, and
`.wasm`. Precompressed `.br` and `.gz` siblings are recorded as package assets
when `precompressed` enables them.

`sloppy package` copies recorded assets into `artifacts/assets/` so packaged
apps carry the source assets they were built from. Rebuild and repackage after
changing files. Sloppy does not browse static directories at runtime and does
not expose directory listings.
