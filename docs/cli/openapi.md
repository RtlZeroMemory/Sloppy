# `sloppy openapi`

Generate an OpenAPI document from a Plan.

```
sloppy openapi <artifacts-dir|plan.json> [--output <path>]
sloppy openapi --plan <path> [--output <path>]
sloppy openapi --artifacts <dir> [--output <path>]
```

Without `--output`, the document prints to stdout. With `--output <file>`,
it writes that path. The file's parent directory must already exist —
the CLI does not create it.

The output is **JSON**, not YAML. The filename is up to you, but the
content is always JSON.

`<path>` is an `app.plan.json` file or a directory containing one.
`--artifacts <dir>` is the explicit artifact-directory form.

OpenAPI is only available for web Plans. Program Plans fail with a clear
diagnostic because they intentionally do not claim a static route graph. For web
Plans with dynamic route metadata, OpenAPI includes only statically known,
representable routes and marks the document policy as partial when dynamic
routes are omitted.

## What it produces

A minimal OpenAPI 3 document in JSON form:

- One `paths` entry per declared route.
- Path parameters extracted from `{name}`, `{name:str}`, `{name:int}`,
  `{name:uuid}`, `{name:alpha}`, and `{name:float}` patterns.
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
| `json`             | `application/json`          | schema `$ref` when declared, otherwise `x-slop-partial` |
| `problem`          | `application/problem+json`  | `{ status, title, detail }`             |
| `text`             | `text/plain`                | `{ type: "string" }`                    |
| `html`             | `text/html`                 | `{ type: "string" }`                    |
| `bytes`            | `application/octet-stream`  | `{ type: "string", format: "binary" }`  |
| `stream`           | `application/octet-stream`  | `{ type: "string", format: "binary" }`  |
| `empty`            | none                        | status and description only             |

JSON responses use the declared Plan response body schema when the schema is
available. They carry `x-slop-partial` when the compiler could not see a
response body schema.

Request body media types are emitted from Plan binding metadata:
`application/json`, `application/x-www-form-urlencoded`, and
`multipart/form-data`. Validation problem responses use
`application/problem+json`.

Plan-visible Sloppy auth providers emit OpenAPI `securitySchemes`.
Protected routes emit route-specific `security` requirements when the
route names schemes; otherwise they use the configured Plan-visible
schemes. Protected operations also include deterministic `401` and `403`
`application/problem+json` responses when those statuses are not already
declared in route metadata. Anonymous routes do not emit an OpenAPI
security requirement.

## Sloppy extensions

The output uses `x-slop-*` extension fields for things OpenAPI doesn't
directly model:

- `x-slop-completeness` — per-path/operation indicators of how
  completely the Plan describes the operation.
- `x-slop-capabilities` — capability tokens the operation reads or
  writes.
- `x-slop-source` — `{ path, line, column }` pointing at the route
  declaration in source.
- `x-slop-auth` — route auth metadata from the Plan, including whether
  auth is required, `allowAnonymous`, schemes, scopes, roles, claims, and
  policy when present.
- `x-slop-optimization-candidates` — Plan-derived hints surfaced for
  tooling.
- `x-slop-partial` — a marker noting the field/section was emitted with
  partial information (e.g. body schema unknown, response shape not
  declared).
- `x-slop-openapi-policy` — top-level metadata about how the document
  was produced, including whether dynamic route metadata was omitted.
- `x-slop-realtime` / `x-slop-transport` (experimental) — realtime route
  metadata for SSE and WebSocket-intent routes. WebSocket operations still
  represent the current unavailable runtime path unless native upgrade
  execution is added later.

These fields are stable across builds for a given source/compiler pair,
suitable for diffing.

Path parameters with Sloppy constraints carry `x-slop-constraint`. Integer
constraints emit OpenAPI integer schemas, UUID constraints emit string schemas
with `format: "uuid"`, alpha constraints emit string schemas with an ASCII
letter pattern, and float constraints emit number schemas.

## Current limits

- Full request body schemas beyond what the Plan emitted from
  `schema.object(...)` (partials get `x-slop-partial`).
- Full response body schemas for responses not declared in Plan metadata
  (those responses emit `x-slop-partial`).
- Security schemes beyond Plan-visible Sloppy auth providers.
- Servers or external-docs metadata.
- Middleware, CORS, RequestId, RequestLogging, and controller behavior are
  reflected only to the extent they are visible in current Plan route metadata;
  OpenAPI does not model the full runtime pipeline semantics.
- Dynamic routes with unknown paths are omitted rather than invented. Static
  routes in the same Plan still appear.

This is enough for documentation and for sanity-checking that an API
change matches the public contract. Pipe it into a fuller OpenAPI tool
if you need richer output.

## Examples

```
sloppy openapi .sloppy
sloppy openapi .sloppy --output openapi.json
sloppy openapi --artifacts .sloppy --output openapi.json
```

## Tips

- Diff the output across builds to spot accidental API changes.
- Pipe through `jq` for filtering.
- Pair with [`sloppy routes`](routes.md) when you want a less verbose view.
