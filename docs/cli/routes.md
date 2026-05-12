# `sloppy routes`

List the routes a Plan declares. Read-only; doesn't enter V8.

```sh
sloppy routes <artifacts-dir|plan.json> [--format text|json] [--dispatch]
sloppy routes --plan <path> [--format text|json] [--dispatch]
sloppy routes --artifacts <dir> [--format text|json] [--dispatch]
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

Routes are sorted in runtime match-precedence order: literal segments before
parameter segments, constrained parameters before unconstrained parameters,
longer/more-specific patterns before shorter patterns, and source order for
remaining ties.

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
as dynamic and records the reason when the Plan has one. Each route also
includes a `constraints` array for path parameters, with `str` for
unconstrained parameters and explicit kinds such as `int`, `uuid`, `alpha`, or
`float` when present in the pattern.

`kind` is `http` for ordinary routes, `sse` for server-sent event routes, and
`websocket` for WebSocket route intent. WebSocket route metadata does not imply
native upgrade execution in this alpha.

For Program Plans, JSON returns `"kind": "program"` and an empty `routes`
array. Text output says no route metadata is expected for a program Plan.

## Dispatch Details

Pass `--dispatch` to include the compiler/runtime dispatch contract. Current
alpha builds emit `native-compiled`: Plan-backed native dispatch with
`routes.slrt` integrity validation. The compiler writes `routes.slrt`, the Plan
records its hash, and the runtime validates the artifact before building an
exact static-path hash table plus a method-specific segment trie from
`app.plan.json` route metadata. Candidate buckets remain an internal fallback
for partial or manually constructed dispatch tables.

```sh
sloppy routes .sloppy --dispatch
sloppy routes .sloppy --dispatch --format json
```

The dispatch block reports route and endpoint counts, exact static paths,
parameter routes, candidate bucket count, segment-trie node count, fallback
availability, native no-JS endpoints, and native URL generation status.

## Use cases

- Sanity-check that a build picked up your routes.
- Diff route metadata across two builds.
- Generate human-readable documentation for an API.
- Wire into CI to fail when routes change unexpectedly.

For OpenAPI output, use [`sloppy openapi`](openapi.md). `routes` is the
lower-level dump.
