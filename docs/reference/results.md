# Results Reference

`Results` helpers (from `stdlib/sloppy/results.js`) return frozen response descriptors.

## Descriptor Shape

Common fields:

- `__sloppyResult: true`
- `kind`
- `status`
- `contentType` (omitted for empty responses)
- `headers` (`undefined` or frozen plain object)
- `body` (omitted for empty responses)

Optional extra fields:

- `location` on `Results.created(...)`

## Helpers

| Helper | Kind | Status behavior | Content type |
| --- | --- | --- | --- |
| `Results.ok(value?, options?)` | `json` | default `200` | `application/json; charset=utf-8` |
| `Results.created(location, value?, options?)` | `json` | default `201` | `application/json; charset=utf-8` |
| `Results.accepted(value?, options?)` | `json` | default `202` | `application/json; charset=utf-8` |
| `Results.noContent()` | `empty` | `204` | none |
| `Results.notFound(value?, options?)` | `json` | default `404` | `application/json; charset=utf-8` |
| `Results.badRequest(value?, options?)` | `json` | default `400` | `application/json; charset=utf-8` |
| `Results.status(code, value?, options?)` | `json` or `empty` | explicit | JSON type for non-empty form |
| `Results.problem(problemOrMessage?, options?)` | `problem` | default `500` | `application/problem+json; charset=utf-8` |
| `Results.text(body, options?)` | `text` | default `200` | `text/plain; charset=utf-8` |
| `Results.json(value, options?)` | `json` | default `200` | `application/json; charset=utf-8` |
| `Results.html(body, options?)` | `html` | default `200` | `text/html; charset=utf-8` |
| `Results.bytes(body, options?)` | `bytes` | default `200` | default `application/octet-stream` |

## Option Validation

Shared option checks:

- `status` must be an integer in `100..999`.
- `headers` must be a plain object when provided. Header names must use safe HTTP
  token characters and cannot be runtime-managed names such as `Content-Type`,
  `Content-Length`, `Connection`, `Transfer-Encoding`, or `Keep-Alive`. Header
  values must be strings without control characters other than horizontal tab.
- `contentType` (when used) must be a non-empty string with no control characters.

Helper-specific checks:

- `Results.created(location, ...)` requires a non-empty safe header value string
  for `location`.
- `Results.bytes(body, ...)` accepts `ArrayBuffer` or any typed-array/DataView (`ArrayBuffer.isView` path).

## Problem Result Normalization

- `Results.problem()` -> `{ title: "Sloppy problem", status }`
- `Results.problem("text")` -> `{ title: "text", status }`
- `Results.problem(object)` -> `{ status, ...object }`
- non-string/non-object problem values throw.

Problem results use `application/problem+json; charset=utf-8`. Sloppy's current
safe handler-error shape is:

```json
{"status":500,"title":"Internal Server Error","code":"SLOPPY_E_HANDLER_ERROR"}
```

`ProblemDetails.defaults()` uses that shape for thrown or rejected route handler
errors when installed on the app. The default policy does not include exception
messages in the response body.

## Plan Response Metadata

The compiler records visible `Results.*` returns as response metadata when the
helper call is statically supported. The recorded `kind` follows the descriptor
kind from this page: `noContent` records `empty`, `badRequest` and `notFound`
record `json`, and `problem` records `problem`.

That metadata feeds `sloppy openapi`, route inspection, and generated handler
evidence. Dynamic response writer APIs are not inferred as Plan response
metadata yet.

## Limits

- `Results.stream` is a bounded descriptor API. It uses native Core stream
  serialization after the handler returns, but it is not a live JavaScript
  streaming writer API.
- No response trailers or chunk-control API.
