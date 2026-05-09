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
| `schema.boolean()` | boolean check |
| `schema.object(shape)` | object shape validation over field schemas |

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

## Route Metadata Validation Hints

Bootstrap route options can carry metadata objects (for example `{ query: someSchema }`), and that metadata appears in route snapshots. This by itself is metadata wiring, not a full automatic HTTP validation contract.

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

## Limits

- No full TypeScript typechecker claim.
- No automatic reflection for arbitrary external schema libraries.
- No decorator-based validation pipeline.
