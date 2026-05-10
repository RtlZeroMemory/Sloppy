# RequestLogging

`RequestLogging` provides app-host middleware that writes one structured log
entry when a request completes.

Import:

```ts
import { RequestLogging } from "sloppy";
```

## `RequestLogging.defaults(options?)`

```ts
app.use(RequestLogging.defaults());
```

The log message is:

```text
request completed
```

Default fields:

- `method`
- `path`
- `status`
- route pattern when known
- route name when available
- request ID when `RequestId` ran earlier in the pipeline
- `durationMs`

Options:

| Option | Default | Behavior |
| --- | --- | --- |
| `includeRoute` | `true` | Includes route pattern and route name when available. |
| `includeDuration` | `true` | Includes elapsed milliseconds from the host clock. |
| `includeRequestId` | `true` | Includes `ctx.requestId` when present. |

Request logging records metadata only. It does not log request bodies or
headers. Authorization, cookie, API key, and proxy authorization header values
stay out of the default log entry.
