# `sloppy routes`

List the routes a Plan declares. Read-only; doesn't enter V8.

```
sloppy routes --plan <path> [--format text|json]
```

`<path>` points to either an `app.plan.json` file directly, or to a
directory containing one (e.g. `.sloppy/`).

## Output

**Text** (default):

```
$ sloppy routes --plan .sloppy/app.plan.json
GET    /health           handler=1 name=Health.Get
GET    /hello/{name}     handler=2 name=Hello.Get
```

Routes are sorted: literal segments before parameter segments, ties broken
by source order (matching the runtime's match precedence).

**JSON**:

```
$ sloppy routes --plan .sloppy/app.plan.json --format json
[
  { "method": "GET", "pattern": "/health", "handlerId": 1, "name": "Health.Get" },
  { "method": "GET", "pattern": "/hello/{name}", "handlerId": 2, "name": "Hello.Get" }
]
```

JSON output is stable; tooling can pipe it through `jq` or feed it into
custom validation.

## Use cases

- Sanity-check that a build picked up your routes.
- Diff route metadata across two builds.
- Generate human-readable documentation for an API.
- Wire into CI to fail when routes change unexpectedly.

For OpenAPI output, use [`sloppy openapi`](openapi.md). `routes` accepts either
`--plan <path>` or `--artifacts <dir>`. `routes` is the lower-level dump.
