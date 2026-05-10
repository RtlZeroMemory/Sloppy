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
GET    /health           handler=1 name=Health.Get
GET    /hello/{name}     handler=2 name=Hello.Get
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
    { "method": "GET", "pattern": "/health", "handlerId": 1, "name": "Health.Get" },
    { "method": "GET", "pattern": "/hello/{name}", "handlerId": 2, "name": "Hello.Get" }
  ]
}
```

JSON output is stable; tooling can pipe it through `jq` or feed it into
custom validation. Dynamic route entries include metadata that marks the route
as dynamic and records the reason when the Plan has one.

For Program Plans, JSON returns `"kind": "program"` and an empty `routes`
array. Text output says no route metadata is expected for a program Plan.

## Use cases

- Sanity-check that a build picked up your routes.
- Diff route metadata across two builds.
- Generate human-readable documentation for an API.
- Wire into CI to fail when routes change unexpectedly.

For OpenAPI output, use [`sloppy openapi`](openapi.md). `routes` is the
lower-level dump.
