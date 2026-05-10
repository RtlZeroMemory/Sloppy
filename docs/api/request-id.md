# RequestId

`RequestId` provides app-host middleware that assigns a request ID and can write
it to the response.

Import:

```ts
import { RequestId } from "sloppy";
```

## `RequestId.defaults(options?)`

```ts
app.use(RequestId.defaults());
```

Default behavior:

- generated IDs use `req-<number>`;
- incoming request IDs are ignored;
- `ctx.requestId` is available to later middleware and handlers;
- `x-request-id` is written to the response.

Options:

| Option | Default | Behavior |
| --- | --- | --- |
| `header` | `"x-request-id"` | Header name to read/write. Must be a safe unmanaged HTTP header name. |
| `responseHeader` | `true` | Writes the response header when enabled. |
| `trustIncoming` | `false` | Uses a safe incoming header value when enabled. |
| `generator` | internal counter | Function that returns a safe non-empty request ID. |

Managed headers such as `content-type`, `content-length`, `connection`,
`transfer-encoding`, and `keep-alive` cannot be used as the request ID header.
Incoming or generated values with control characters are rejected or ignored.

Compiler source-input support exists for the current static middleware subset;
dynamic shapes outside that subset fail closed.
