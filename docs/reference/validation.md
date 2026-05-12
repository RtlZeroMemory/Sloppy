# Validation Reference

Validation currently has two implementation-backed surfaces:

- runtime schema objects from `stdlib/sloppy/schema.js`
- compiler-extracted request binding metadata in `compiler/src/sloppyc.rs`

## Runtime `schema` API

Import:

```ts
import { Schema } from "sloppy";
```

`schema` is the lowercase compatibility alias for `Schema`.

Supported constructors:

| Constructor | Behavior |
| --- | --- |
| `Schema.string()` | string type check |
| `Schema.string().min(n)` | minimum string length (`n` must be non-negative integer) |
| `Schema.string().max(n)` | maximum string length (`n` must be non-negative integer) |
| `Schema.string().minLength(n)` / `maxLength(n)` | string length aliases |
| `Schema.string().email()` | simple email regex check |
| `Schema.string().uuid()` | UUID format check |
| `Schema.number().min(n).max(n)` | finite-number check with optional bounds |
| `Schema.int()` / `Schema.integer()` | integer check |
| `Schema.boolean()` / `Schema.bool()` | boolean check |
| `Schema.array(itemSchema)` | array validation over an item schema |
| `Schema.object(shape)` | object shape validation over field schemas |
| `Schema.enum(values)` | literal value membership |
| `Schema.literal(value)` | exact literal check |

Every schema returned by these constructors also supports `.optional()`. Optional schemas
accept `undefined`; other values are validated by the wrapped schema. Schemas also
support `.nullable()` and `.default(value)`.

Validation result shape:

- success: `{ ok: true, value }`
- failure: `{ ok: false, issues }`

Issue entry shape:

- `path`: array path
- `code`: for example `type`, `string.min`, `string.email`
- `message`: human-readable reason

`Schema.object(shape)` requirements:

- `shape` must be a plain object
- each key must be non-empty
- each value must expose both `validate(...)` and `__validateAtPath(...)`

Optional object fields use the normal field schema:

```ts
const User = Schema.object({
  name: Schema.string().min(1),
  tags: Schema.array(Schema.string()).optional(),
});
```

## Request Body Validation

`ctx.body.validate(schema)` parses the JSON body and returns the validated
value. Invalid JSON and schema failures are surfaced as `400
application/problem+json` validation problems in the app-host/test-host path.

Route registrations can attach metadata:

```ts
app.post("/users", createUser)
  .accepts(CreateUser)
  .returns(User);
```

Static `Schema.*` declarations, `ctx.body.validate(CreateUser)`,
`.accepts(CreateUser)`, and `.returns(User)` are compiler-visible when their
schema arguments are local identifiers. Static `Schema.literal(...)` and
`Schema.enum([...])` extraction supports string, number, and boolean values;
the runtime schema API also accepts `null` literals.

Compiled/native runs use compiler-visible request schemas as `jsonRequest`
metadata. Schema-known JSON request bodies are validated before the handler
runs, with safe problem details for malformed JSON, missing required fields,
type mismatches, literal/enum mismatch, bounds, array length, nullable/optional
fields, and unknown-field policy failures. Response schemas remain metadata or
explicit fallback for dynamic handler returns unless the compiler proves a
static native JSON response.

## Route Metadata Validation Hints

Bootstrap route options can carry metadata objects (for example `{ query:
someSchema }`), and that metadata appears in route snapshots. Prefer
`.accepts(...)` and `.returns(...)` for request/response schema metadata.

## Compiler Binding Validation (Framework typed handlers)

Supported wrapper markers in typed signatures:

- `Route<T>`
- `Query<T>`
- `Body<T>`
- `Header<"name">` and `Header<"name", T>`
- `Service<T>`
- `Config<...>`

Additional typed markers:

- `RequestContext`
- `Sqlite<"...">`, `Postgres<"...">`, `SqlServer<"...">`
- `WorkQueue<"...">`

Current enforced constraints include:

- no rest parameters in typed handler signatures
- parameters must be simple identifiers with type annotations
- no destructured/default parameter forms
- at most one body parameter
- explicit `Route<T>` parameter names must match route segments
- all route segments must be bound by handler signature
- `Header<T>` requires a string-literal header name when used as wrapper

Compiler rejects unsupported shapes with `SLOPPYC_E_*` diagnostics and source spans.

`Config<"KEY">` bindings are validated as compiler metadata too. Generated code
reads the environment value first and uses a literal default recorded from
`app.config.getString("KEY", "default")` when the environment value is absent.

## Limits

- Full TypeScript typechecking is outside the current validation surface.
- Arbitrary external schema libraries require explicit adapter work.
- Decorator-based validation belongs to a later framework design if the runtime adopts it.
- Middleware, CORS, RequestId, RequestLogging, and the controller static
  subsets have compiler coverage where the Plan can represent them. Dynamic
  shapes and unsupported captures are rejected at build time with specific
  diagnostics.
