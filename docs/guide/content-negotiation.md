# Content Negotiation

Sloppy chooses response media types from the result helper a handler returns.
It does not guess from the JavaScript value.

```ts
app.get("/users", () => Results.ok([{ id: 1 }]));
app.get("/robots.txt", () => Results.text("User-agent: *"));
app.get("/download", () => Results.bytes(fileBytes));
```

## Response Media Types

| Helper | Default media type |
| --- | --- |
| `Results.json`, `Results.ok`, `Results.created`, `Results.accepted`, `Results.status(value)` | `application/json; charset=utf-8` |
| `Results.problem` | `application/problem+json; charset=utf-8` |
| `Results.text` | `text/plain; charset=utf-8` |
| `Results.html` | `text/html; charset=utf-8` |
| `Results.bytes` | `application/octet-stream` |
| `Results.stream` | `application/octet-stream` |
| `Results.noContent`, `Results.status(code)` without a value | no body media type |

Use `contentType` only when the bytes still match the helper's body shape:

```ts
return Results.json(body, {
    contentType: "application/vnd.example.user+json; charset=utf-8",
});
```

## Accept

By default, Sloppy uses a compatibility fallback: an unsupported `Accept` header
does not change the helper's media type and does not reject the response. This
keeps alpha apps predictable while clients are still being wired up.

Enable strict response negotiation when you want unsupported `Accept` values to
fail:

```ts
const app = Sloppy.create({
    contentNegotiation: {
        strictAccept: true,
    },
});
```

`app.useContentNegotiation({ strictAccept: true })` applies the same setting
before the app is frozen. Strict mode accepts exact media types, type wildcards
such as `text/*`, and `*/*`. Problem details use their exact
`application/problem+json` media type. Unsupported values return
`406 Not Acceptable`.

## Request Content-Type

Request body helpers are media-type gated:

| Helper | Required request media type |
| --- | --- |
| `ctx.request.json()` / `ctx.body.json()` | `application/json` or `application/*+json` |
| `ctx.request.text()` | body already accepted by the runtime classifier; use `text/plain` for application text |
| `ctx.request.form()` | `application/x-www-form-urlencoded` |
| `ctx.request.multipart()` | `multipart/form-data` with a `boundary` parameter |
| `ctx.request.bytes()` | raw accepted request bytes |

Malformed JSON returns `400 application/problem+json`. Unsupported body media
types return `415 Unsupported Media Type` before the handler runs.

## JSON Options

The alpha JSON serializer is explicit about values that standard
`JSON.stringify` handles badly:

```ts
const app = Sloppy.create({
    json: {
        casing: "camelCase",
        includeNulls: true,
        dateFormat: "iso8601",
        bigint: "string",
        bytes: "base64",
    },
});
```

Dates serialize as ISO 8601 strings, BigInts as strings by default, bytes as
base64 by default, `undefined` object fields are omitted, and circular
references fail clearly.
