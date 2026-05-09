# `sloppy openapi`

Generate an OpenAPI document from a Plan.

```
sloppy openapi --plan <path> [--output <path>]
```

Without `--output`, the document prints to stdout. With `--output <file>`,
it writes the file (creating parent directories as needed).

`<path>` is an `app.plan.json` file or a directory containing one.

## What it produces

A minimal OpenAPI 3 skeleton:

- One `paths` entry per declared route.
- Path parameters extracted from `{name}` and `{name:int}` patterns.
- Method names mapped to OpenAPI operations.
- Route names from `.withName(...)` become operation IDs.
- Tags from `app.group(...).withTags(...)` and route-level tags propagate.

```yaml
openapi: 3.0.3
info:
  title: <inferred or default>
  version: <inferred or default>
paths:
  /hello/{name}:
    get:
      operationId: Hello.Get
      parameters:
        - in: path
          name: name
          required: true
          schema: { type: string }
```

## What it doesn't produce yet

- Request body schemas beyond what the Plan emitted from `schema.object(...)`.
- Response schemas (the Plan only carries the visible `Results.*` status
  codes today).
- Security schemes.
- Servers or external docs metadata.

This is enough for documentation and for sanity-checking that an API change
matches the public contract. Treat it as a starting point: feed it into a
fuller OpenAPI tool if you need richer output.

## Examples

```
sloppy openapi --plan .sloppy/app.plan.json
sloppy openapi --plan .sloppy --output openapi.yaml
```

## Tips

- Diff the output across builds to spot accidental API changes.
- Pipe through `yq` or `jq` for filtering.
- Pair with [`sloppy routes`](routes.md) when you want a less verbose view.
