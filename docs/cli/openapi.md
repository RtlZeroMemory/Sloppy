# `sloppy openapi`

Generate an OpenAPI document from a Plan.

```
sloppy openapi --plan <path> [--output <path>]
```

Without `--output`, the document prints to stdout. With `--output <file>`,
it writes that path. The file's parent directory must already exist —
the CLI does not create it.

The output is **JSON**, not YAML. The filename is up to you, but the
content is always JSON.

`<path>` is an `app.plan.json` file or a directory containing one.

## What it produces

A minimal OpenAPI 3 document in JSON form:

- One `paths` entry per declared route.
- Path parameters extracted from `{name}` / `{name:str}` / `{name:int}`
  patterns.
- Method names mapped to OpenAPI operations.
- Route names from `.withName(...)` or route metadata options such as
  `{ name: "Users.List" }` become operation IDs.
- Tags from `app.group(...).withTags(...)`, route metadata options, and
  route-level tags propagate.

```json
{
  "openapi": "3.0.3",
  "info": { "title": "...", "version": "..." },
  "paths": {
    "/hello/{name}": {
      "get": {
        "operationId": "Hello.Get",
        "parameters": [
          {
            "in": "path",
            "name": "name",
            "required": true,
            "schema": { "type": "string" }
          }
        ]
      }
    }
  }
}
```

## Response content

Each response with a Plan-recorded `Results.*` kind gets a `content` entry
keyed by media type:

| Plan response kind | Media type                  | Schema                                  |
| ------------------ | --------------------------- | --------------------------------------- |
| `json`             | `application/json`          | `x-slop-partial` until shape declared   |
| `problem`          | `application/problem+json`  | `{ status, title, detail }`             |
| `text`             | `text/plain`                | `{ type: "string" }`                    |
| `html`             | `text/html`                 | `{ type: "string" }`                    |
| `bytes`            | `application/octet-stream`  | `{ type: "string", format: "binary" }`  |
| `empty`            | none                        | status and description only             |

JSON responses carry `x-slop-partial` until the compiler records a declared
response body schema.

## Sloppy extensions

The output uses `x-slop-*` extension fields for things OpenAPI doesn't
directly model:

- `x-slop-completeness` — per-path/operation indicators of how
  completely the Plan describes the operation.
- `x-slop-capabilities` — capability tokens the operation reads or
  writes.
- `x-slop-source` — `{ path, line, column }` pointing at the route
  declaration in source.
- `x-slop-optimization-candidates` — Plan-derived hints surfaced for
  tooling.
- `x-slop-partial` — a marker noting the field/section was emitted with
  partial information (e.g. body schema unknown, response shape not
  declared).
- `x-slop-openapi-policy` — top-level metadata about how the document
  was produced.

These fields are stable across builds for a given source/compiler pair,
suitable for diffing.

## What it doesn't produce yet

- Full request body schemas beyond what the Plan emitted from
  `schema.object(...)` (partials get `x-slop-partial`).
- Declared `application/json` response body schemas (the Plan carries the
  visible `Results.*` kinds and status codes today; the JSON entry is a
  `x-slop-partial`).
- Security schemes.
- Servers or external-docs metadata.
- Middleware, CORS, RequestId, RequestLogging, and controller behavior that the
  current compiler rejects before Plan emission.

This is enough for documentation and for sanity-checking that an API
change matches the public contract. Pipe it into a fuller OpenAPI tool
if you need richer output.

## Examples

```
sloppy openapi --plan .sloppy/app.plan.json
sloppy openapi --plan .sloppy --output openapi.json
```

## Tips

- Diff the output across builds to spot accidental API changes.
- Pipe through `jq` for filtering.
- Pair with [`sloppy routes`](routes.md) when you want a less verbose view.
