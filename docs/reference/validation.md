# Validation Reference

Validation currently has two implementation-backed surfaces:

- runtime schema objects from `stdlib/sloppy/schema.js`
- compiler-extracted request binding metadata in `compiler/src/sloppyc.rs`

## Runtime `schema` API

Import:

```ts
import { schema } from "sloppy";
```

Supported constructors:

| Constructor | Behavior |
| --- | --- |
| `schema.string()` | string type check |
| `schema.string().min(n)` | minimum string length (`n` must be non-negative integer) |
| `schema.string().email()` | simple email regex check |
| `schema.number()` | finite-number check |
| `schema.int()` | integer check |
| `schema.boolean()` | boolean check |
| `schema.bool()` | alias for `schema.boolean()` |
| `schema.array(itemSchema)` | array validation over an item schema |
| `schema.object(shape)` | object shape validation over field schemas |

Every schema returned by these constructors also supports `.optional()`. Optional schemas
accept `undefined`; other values are validated by the wrapped schema.

Validation result shape:

- success: `{ ok: true, value }`
- failure: `{ ok: false, issues }`

Issue entry shape:

- `path`: array path
- `code`: for example `type`, `string.min`, `string.email`
- `message`: human-readable reason

`schema.object(shape)` requirements:

- `shape` must be a plain object
- each key must be non-empty
- each value must expose both `validate(...)` and `__validateAtPath(...)`

Optional object fields use the normal field schema:

```ts
const User = schema.object({
  name: schema.string().min(1),
  tags: schema.array(schema.string()).optional(),
});
```

## Route Metadata Validation Hints

Bootstrap route options can carry metadata objects (for example `{ query: someSchema }`), and that metadata appears in route snapshots. Automatic request validation and validation-to-ProblemDetails mapping are planned as separate framework/runtime work.

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
- Middleware, CORS, RequestId, RequestLogging, and controller static subsets
  have compiler coverage where the Plan can represent them. Dynamic shapes and
  unsupported captures fail closed with specific diagnostics.
