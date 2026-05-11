# `sloppy routes`

List the routes a Plan declares. Read-only; doesn't enter V8.

```sh
sloppy routes <artifacts-dir|plan.json> [--format text|json]
sloppy routes --plan <path> [--format text|json]
sloppy routes --artifacts <dir> [--format text|json]
```

Use `sloppy routes .sloppy` for the common case. `--plan <path>` and
`--artifacts <dir>` remain explicit forms for scripts.

## Output

**Text** (default):

```text
$ sloppy routes .sloppy
ORDER  METHOD  KIND         PATTERN              HANDLER  COMPLETE  MODULE  SOURCE  BINDINGS  RESPONSE  NAME
0      GET     http         /health              1        complete          app.js:4:1  -  200/json/json  Health.Get
1      GET     http         /hello/{name}        2        complete          app.js:5:1  -  200/json/json  Hello.Get
2      GET     sse          /events              3        complete          app.js:6:1  -  stream/text-event-stream  Events
```

Routes are sorted: literal segments before parameter segments, ties broken
by source order (matching the runtime's match precedence).

When the compiler sees runnable dynamic route registration that it cannot fully
describe, text output uses known values where available and `<dynamic>` for
unknown method or pattern pieces. The completeness column/reason shows whether
metadata is complete, partial, dynamic, or opaque.

**JSON**:

```text
$ sloppy routes .sloppy --format json
{
  "kind": "web",
  "routes": [
    { "method": "GET", "pattern": "/health", "kind": "http", "handlerId": 1, "name": "Health.Get" },
    { "method": "GET", "pattern": "/hello/{name}", "kind": "http", "handlerId": 2, "name": "Hello.Get" },
    { "method": "GET", "pattern": "/events", "kind": "sse", "handlerId": 3 }
  ]
}
```

JSON output is stable; tooling can pipe it through `jq` or feed it into
custom validation. Dynamic route entries include metadata that marks the route
as dynamic and records the reason when the Plan has one.

`kind` is `http` for ordinary routes, `sse` for server-sent event routes, and
`websocket` for WebSocket route intent. WebSocket route metadata does not imply
native upgrade execution in this alpha.

For Program Plans, JSON returns `"kind": "program"` and an empty `routes`
array. Text output says no route metadata is expected for a program Plan.

## Use cases

- Sanity-check that a build picked up your routes.
- Diff route metadata across two builds.
- Generate human-readable documentation for an API.
- Wire into CI to fail when routes change unexpectedly.

For OpenAPI output, use [`sloppy openapi`](openapi.md). `routes` is the
lower-level dump.
