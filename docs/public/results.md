# Results

Status: Bootstrap result helper set implemented; dev-only run consumes supported
`Results.*` descriptors through the EPIC-23 native response writer.

Bootstrap status: `stdlib/sloppy/results.js` exports a frozen `Results` object with
`Results.ok(...)`, `Results.created(...)`, `Results.accepted(...)`,
`Results.noContent(...)`, `Results.notFound(...)`, `Results.badRequest(...)`,
`Results.problem(...)`, `Results.text(...)`, `Results.json(...)`, and
`Results.html(...)`.

Purpose: document current result descriptor helpers and the future path where handler
return values become native response descriptors.

Implemented API example:

```ts
app.mapGet("/hello", () => Results.text("hello"));
app.mapGet("/health", () => Results.json({ status: "ok" }, { status: 200 }));
```

`examples/hello/app.js` uses `Results.text("Hello from Sloppy")` to demonstrate the text
descriptor shape. `examples/ergonomics/app.js` demonstrates `Results.ok`,
`Results.accepted`, and `Results.noContent`.

`sloppy run --artifacts` executes EPIC-21/24 compiler output in V8-enabled builds. The
runtime loads `stdlib/sloppy/internal/runtime-classic.js` from the staged bootstrap stdlib
root before evaluating generated `app.js`; generated code reads `Results` from
`globalThis.__sloppy_runtime` instead of embedding a compiler-owned shim. The V8 bridge
converts supported descriptors into native
`SlHttpResponse` values, and the native writer emits HTTP/1.1 bytes with status,
`Connection: close`, `Content-Type` when present, `Content-Length`, CRLF separators, and
body bytes. Plain string handler returns remain supported as a compatibility fallback and
become `200 text/plain; charset=utf-8`.

All helpers return frozen plain descriptors with the `__sloppyResult: true` identity marker.
Descriptors are JavaScript values for bootstrap tests, examples, and future bridge
conversion. They do not write responses.

Implemented helpers:

- `Results.ok(value?, options?)` returns a JSON descriptor with status `200` by default.
- `Results.created(location, value?, options?)` returns a JSON descriptor with status `201`
  by default and a `location` field.
- `Results.accepted(value?, options?)` returns a JSON descriptor with status `202` by
  default.
- `Results.noContent()` returns an empty descriptor with status `204` and no `body` field.
- `Results.notFound(valueOrProblem?, options?)` returns a JSON descriptor with status `404`
  by default.
- `Results.badRequest(valueOrProblem?, options?)` returns a JSON descriptor with status
  `400` by default.
- `Results.problem(problemOrMessage?, options?)` returns a problem descriptor with status
  `500` by default and `application/problem+json; charset=utf-8`.
- `Results.text(body, options?)` returns a text descriptor.
- `Results.json(value, options?)` returns a JSON descriptor without stringifying `value`.
- `Results.html(body, options?)` returns an HTML descriptor.

`Results.text(body, options?)` shape:

```js
{
  __sloppyResult: true,
  kind: "text",
  status: 200,
  body: "hello",
  contentType: "text/plain; charset=utf-8"
}
```

`Results.json(value, options?)` returns a frozen plain descriptor without stringifying the
value:

```js
{
  __sloppyResult: true,
  kind: "json",
  status: 200,
  body: value,
  contentType: "application/json; charset=utf-8"
}
```

Both helpers accept `options.status`; status must be an integer from 100 to 999 and
defaults to each helper's documented default. `Results.text` and `Results.html` store
`String(body)`. `Results.json` and JSON-shaped status helpers preserve the provided
JavaScript value as `body`; the descriptor is frozen, but object values are not deep-frozen.
In the dev-only EPIC-23 V8 response conversion path, omitted or `undefined` JSON-shaped
descriptor bodies serialize deterministically as JSON `null`.
`options.headers` may be a plain object and is shallow-copied/frozen as descriptor metadata.
There is no header normalization class.

Implemented in the dev run path now:

- `text`, `json`, `ok`, `noContent`, and `problem` descriptors;
- `400`, `404`, `405`, `500`, `200`, `201`, `202`, and `204` response status writing;
- JSON body serialization through V8 `JSON.stringify` for JSON/problem descriptors;
- omitted or `undefined` JSON/problem descriptor bodies serialized as `null`;
- `204` responses with no body and no `Content-Length`;
- Content-Type CR/LF rejection before bytes are written.

Unsupported result descriptor kinds fail safely with a dev `500` response. `Results.html`,
custom headers, streaming, files, redirects, cookies, content negotiation, and header
normalization beyond Content-Type remain deferred.

Related internal docs: `docs/developer-ergonomics.md`, `docs/execution-model.md`.
