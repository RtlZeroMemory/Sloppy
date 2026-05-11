# ProblemDetails

`ProblemDetails` configures RFC 7807-style error responses for the app-host
surface that supports it.

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

ProblemDetails integration is pre-alpha and is tied to the app-host/framework
surface that consumes the descriptor.

Request validation failures from `ctx.body.validate(schema)` are not treated as
handler faults. They return `400 application/problem+json` with
`SLOPPY_E_VALIDATION_FAILED` whether or not `ProblemDetails.defaults()` is
installed.
