# Schema

A small validation library for shaping handler input. Schemas are values:
build them once, call `.validate(value)` to check.

```ts
import { Schema } from "sloppy";

const userInput = Schema.object({
    name:  Schema.string().min(1),
    email: Schema.string().email(),
    age:   Schema.integer().min(0).optional(),
});

const result = userInput.validate({ name: "Ada", email: "a@b.co", age: 36 });

if (result.ok) {
    // result.value is the validated object
} else {
    // result.issues is an array of { path, code, message }
}
```

## Result shape

Every schema's `validate(value)` returns one of:

```ts
{ ok: true,  value: T }
{ ok: false, issues: ReadonlyArray<Issue> }

interface Issue {
    path:    ReadonlyArray<string | number>;
    code:    string;
    message: string;
}
```

`path` points to the field that failed. `code` is stable; `message` is
human-friendly.

## Primitives

```ts
Schema.string()
Schema.string().min(1).max(100)
Schema.string().minLength(1).maxLength(100)
Schema.string().email()
Schema.string().uuid()

Schema.number().min(0).max(100)
Schema.int()
Schema.integer()     // alias

Schema.boolean()
Schema.bool()        // alias
Schema.enum(["admin", "user"])
Schema.literal("admin")
```

`schema` remains as a lowercase alias for existing code. Each constructor
returns a frozen schema object. Chained methods return new schemas
(immutable).

| Method on `string()` | Effect                              | Issue code      |
| -------------------- | ----------------------------------- | --------------- |
| `.min(n)`            | reject strings shorter than `n`     | `string.min`    |
| `.max(n)`            | reject strings longer than `n`      | `string.max`    |
| `.email()`           | basic email format check            | `string.email`  |
| `.uuid()`            | UUID format check                   | `string.uuid`   |

## Arrays

```ts
const tags = schema.array(schema.string());
```

`schema.array(itemSchema)` validates each entry against `itemSchema`. Failed
items report a numeric segment in `issue.path` (e.g. `["tags", 2]`).

## Optional fields

Every schema exposes `.optional()`, `.nullable()`, and `.default(value)`.
An optional schema accepts `undefined`; a nullable schema accepts `null`;
a defaulted schema substitutes its default when the value is `undefined`.

```ts
const post = schema.object({
    title: schema.string().min(1),
    tags:  schema.array(schema.string()).optional(),
});
```

## Objects

```ts
const post = schema.object({
    title:    schema.string().min(1),
    body:     schema.string(),
    tags:     schema.object({ kind: schema.string() }),
});
```

Validation runs top-down. Every issue reported has the full path; nested
failures get nested paths.

## Request body validation

`ctx.body.validate(schema)` parses the JSON body, validates it, and returns the
validated value. Invalid input throws a Sloppy validation error that the app
host and native runtime map to a `400 application/problem+json` response. The
problem body includes `path`, `code`, and `message` for each issue and does not
echo request body values.

```ts
app.post("/users", async (ctx) => {
    const input = await ctx.body.validate(userInput);
    return Results.created("/users/1", input);
}).accepts(userInput);
```

Use `.accepts(schema)` and `.returns(schema)` on the route registration when
you want request and response schemas to appear in route snapshots, compiler
Plan metadata, and OpenAPI output.

In compiled/native runs, `.accepts(schema)` and compiler-visible
`ctx.body.validate(SchemaName)` calls also drive route-level native JSON
request validation. The generated Plan records the request JSON mode, schema
name, materialization policy, unknown-field policy, and JSON body, depth,
string, and array limits. When the route is schema-known, invalid JSON fails
before the handler runs and the problem response does not include raw body
values.

`.returns(schema)` remains response metadata for dynamic handler return values
unless the compiler can prove a static JSON result. Static `Results.json(...)`
and `Results.ok(...)` values can use the native preencoded JSON response writer;
dynamic or unsupported response shapes carry a fallback reason instead of
claiming native enforcement.

## Limits

The schema library covers the common shapes a handler needs. It is
deliberately small. If you need:

- async validators
- discriminated unions
- transforms (parse-don't-validate)
- localization

…use a third-party library like Zod or Valibot at the application level —
nothing stops you. The Sloppy compiler will generate Plan-level validation
metadata from compiler-visible `Schema.*` declarations,
`ctx.body.validate(SchemaName)`, `.accepts(SchemaName)`, and
`.returns(SchemaName)` calls. See
[reference/validation.md](../reference/validation.md).
