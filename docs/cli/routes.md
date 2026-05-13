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
ORDER  METHOD  KIND         PATTERN              HANDLER  COMPLETE  MODULE  SOURCE  BINDINGS  RESPONSE  JSON  RATE_LIMIT  NAME
0      GET     http         /health              1        complete          app.js:4:1  -  200/json/json  req:none/none resp:native-static/preencoded  -  Health.Get
1      POST    http         /users               2        complete          app.js:5:1  body:body  200/json/json  req:native-schema/materialize-once resp:fallback/none resp-fallback:handler-return-shape-dynamic  tokenBucket/default/user  Users.Create
2      GET     sse          /events              3        complete          app.js:6:1  -  stream/text-event-stream  req:none/none resp:none/none  fixedWindow/default/ip  Events
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
    {
      "method": "POST",
      "pattern": "/users",
      "kind": "http",
      "handlerId": 2,
      "name": "Users.Create",
      "jsonRequest": {
        "mode": "native-schema",
        "materialization": "materialize-once",
        "schema": "CreateUser"
      },
      "jsonResponse": {
        "mode": "fallback",
        "writer": "none",
        "schema": "User",
        "fallbackReason": "handler-return-shape-dynamic"
      }
    }
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
`websocket` for WebSocket route intent. Native `sloppy run` executes WebSocket
routes only through HTTP/1.1 Upgrade; direct non-Upgrade HTTP dispatch is not a
normal route call.

Each route also includes `jsonRequest` and `jsonResponse` objects. The text
`JSON` column prints `req:<mode>/<materialization>` and
`resp:<mode>/<writer>`, followed by fallback reason codes when a route cannot
use a native JSON path. JSON output includes the same values plus schema names
where the Plan has them.

Routes with Plan-visible rate limits include a `rateLimit` array in JSON. Text
output prints `algorithm/store/partition`, or `/partial` when the compiler
could not statically identify every policy detail. Raw partition values are not
stored in the Plan.

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
availability, native no-JS endpoints, native URL generation status, and
aggregate JSON request/response native/generic/fallback counts.

## Use cases

- Sanity-check that a build picked up your routes.
- Diff route metadata across two builds.
- Generate human-readable documentation for an API.
- Wire into CI to fail when routes change unexpectedly.

For OpenAPI output, use [`sloppy openapi`](openapi.md). `routes` is the
lower-level dump.
