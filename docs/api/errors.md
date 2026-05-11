# Errors

Sloppy exposes an app-level error policy through `app.useErrors(...)` and typed
exception mappings through `app.mapError(...)`.

```ts
import { Config, Results, Sloppy } from "sloppy";

const app = Sloppy.create();

app.useErrors({
  includeDetails: Config.boolean("Errors:IncludeDetails", false),
});

app.mapError(NotFoundError, (err, ctx) =>
  Results.problem({
    status: 404,
    title: "Not found",
    detail: err.message,
    requestId: ctx.requestId,
  }, { status: 404 })
);
```

## `app.useErrors(options?)`

Enables the app-host error policy for routes registered on the app.

| Option | Default | Behavior |
| --- | --- | --- |
| `includeDetails` | `false` | Boolean or `Config.boolean(...)` reference. When true, handler exception messages can be included in generated 500 responses. Provider errors stay redacted. |
| `detail` | derived from `includeDetails` | `"never"`, `"development"`, or `"always"`. This is the lower-level detail policy used by `ProblemDetails.defaults(...)`. |
| `missingRoute` | `true` | In the app test host, missing routes return a ProblemDetails-style `404` instead of plain text. |
| `maxBodyBytes` | `1048576` | In the app test host, larger request bodies return `413 application/problem+json`. |

When enabled, Sloppy maps framework failures to problem responses: unhandled
exceptions use `500`, validation uses `400`, auth uses `401` or `403`, missing
routes use `404`, oversized bodies use `413`, unsupported media types use
`415`, and provider-shaped errors use a redacted `500`.

If `RequestId.defaults(...)` runs before the error, generated problem bodies
include `requestId`.

## `app.mapError(type, mapper)`

Registers a typed error mapper. `type` must be an error constructor and `mapper`
must return either a `Results.*` descriptor or a plain problem object.

Mappings run before the default unhandled-exception mapping. They do not replace
the built-in validation/auth/media/body mappings.

## Logging

The error policy writes one safe structured `request failed` log entry when
`ctx.log.error(...)` is available. The log entry carries status, code, request
ID, route metadata, and the error name. It does not include exception messages,
request bodies, request headers, provider messages, tokens, cookies, or
connection strings.
