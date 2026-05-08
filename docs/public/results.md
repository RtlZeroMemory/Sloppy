# Results

Status: bootstrap result helper set implemented; bounded run consumes supported
`Results.*` descriptors through the native response writer.

Bootstrap status: `stdlib/sloppy/results.js` exports a frozen `Results` object with
`Results.ok(...)`, `Results.created(...)`, `Results.accepted(...)`,
`Results.noContent(...)`, `Results.notFound(...)`, `Results.badRequest(...)`,
`Results.status(...)`, `Results.problem(...)`, `Results.text(...)`, `Results.json(...)`, and
`Results.html(...)`.

Purpose: document current result descriptor helpers and the future path where handler
return values become native response descriptors.

Current target contract:

- core helpers are `text`, `json`, `ok`, `created`, `accepted`, `noContent`, `notFound`,
  `badRequest`, `problem`, and `status`;
- supported descriptors become native HTTP responses through the V8 bridge;
- custom headers are supported only through `options.headers` on supported descriptors;
- unsupported descriptors fail with diagnostics and a safe framework error response;
- files, streams, redirects, cookies, compression, content negotiation, middleware
  filters, and production error page customization remain deferred.

Implemented API example:

```ts
app.mapGet("/hello", () => Results.text("hello"));
app.mapGet("/health", () => Results.json({ status: "ok" }, { status: 200 }));
```

`examples/hello/app.js` uses `Results.text("Hello from Sloppy")` to demonstrate the text
descriptor shape. `examples/ergonomics/app.js` demonstrates `Results.ok`,
`Results.accepted`, and `Results.noContent`.

`sloppy run --artifacts` executes compiler output in V8-enabled builds. The
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
- `Results.status(statusCode, value?, options?)` returns an empty descriptor when `value` is
  omitted and a JSON descriptor otherwise.
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

Both helpers accept `options.status`; descriptor creation accepts any integer from 100 to
999 so unsupported-but-explicit statuses can be rejected by the runtime writer with a
stable diagnostic instead of at helper construction time. `Results.text` and
`Results.html` store `String(body)`. `Results.json` and JSON-shaped status helpers
preserve the provided JavaScript value as `body`; the descriptor is frozen, but object
values are not deep-frozen. In the current V8 response conversion path, omitted or
`undefined` JSON-shaped descriptor bodies serialize deterministically as JSON `null`.
`options.headers` may be a plain object and is shallow-copied/frozen as descriptor metadata.
There is no header normalization class.

Implemented in the dev run path now:

- `text`, `html`, `json`, `ok`, `created`, `accepted`, `noContent`, `notFound`,
  `badRequest`, `status`, and `problem` descriptors;
- `200`, `201`, `202`, `204`, `400`, `404`, `405`, `408`, `413`, `415`, `500`, and `501`
  response status writing;
- JSON body serialization through V8 `JSON.stringify` for JSON/problem descriptors;
- omitted or `undefined` JSON/problem descriptor bodies serialized as `null`;
- `204` responses with no body and no `Content-Length`;
- custom response headers through `options.headers`, with `Connection`, `Content-Type`, and
  `Content-Length` reserved for the native writer;
- `Location` header emission from `Results.created(...)`;
- Content-Type and header CR/LF rejection before bytes are written;
- invalid result descriptors fail safely with `SLOPPY_E_INVALID_HTTP_RESULT` and a safe dev
  `500` response.

Conformance coverage verifies `Results.text` through the executable hello example,
`Results.json` through the executable request-context example, and an invalid descriptor
through a V8-gated checked-in artifact fixture that must return a safe dev `500`.

Unsupported result descriptor kinds fail safely with a dev `500` response. Streaming,
files, redirects, cookies, content negotiation, and header normalization beyond
case-insensitive request lookup and managed response-header rejection remain deferred.

Related internal docs: `docs/developer-ergonomics.md`, `docs/execution-model.md`.
