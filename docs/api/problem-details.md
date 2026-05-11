# ProblemDetails

`ProblemDetails` is the compatibility descriptor for RFC 7807-style handler
error responses. New application code should prefer [`app.useErrors(...)`](errors.md)
because it also covers typed mappings and app-host status mappings.

Import:

```ts
import { ProblemDetails } from "sloppy";
```

## `ProblemDetails.defaults(options?)`

Returns a frozen descriptor:

```ts
const problemDetails = ProblemDetails.defaults({
  detail: "development",
});
```

Options:

| Option | Default | Behavior |
| --- | --- | --- |
| `detail` | `"never"` | One of `"never"`, `"development"`, or `"always"`. Controls when error detail is included. |

`options` must be a plain object when provided. Invalid `detail` values throw
`TypeError`.

ProblemDetails integration is part of the current public alpha,
pre-production app-host and framework surface that consumes the descriptor.

`ProblemDetails.defaults(...)` uses the same safe handler-error machinery as
`app.useErrors(...)`, with missing-route handling disabled for compatibility.

Request validation failures from `ctx.body.validate(schema)` are not treated as
handler faults. They return `400 application/problem+json` with
`SLOPPY_E_VALIDATION_FAILED` whether or not `ProblemDetails.defaults()` is
installed.
