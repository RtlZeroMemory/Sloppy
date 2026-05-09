# Schema

A small validation library for shaping handler input. Schemas are values:
build them once, call `.validate(value)` to check.

```ts
import { schema } from "sloppy";

const userInput = schema.object({
    name:  schema.string().min(1),
    email: schema.string().email(),
    age:   schema.number(),
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
schema.string()
schema.string().min(1)
schema.string().email()

schema.number()

schema.boolean()
```

Each returns a frozen schema object. Chained methods return new schemas
(immutable).

| Method on `string()` | Effect                              | Issue code      |
| -------------------- | ----------------------------------- | --------------- |
| `.min(n)`            | reject strings shorter than `n`     | `string.min`    |
| `.email()`           | basic email format check            | `string.email`  |

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

## Limits

The schema library covers the common shapes a handler needs. It is
deliberately small. If you need:

- async validators
- discriminated unions
- transforms (parse-don't-validate)
- localization

…use a third-party library like Zod or Valibot at the application level —
nothing stops you. The Sloppy compiler will generate Plan-level validation
metadata from typed handler parameters when the source uses
`schema.object({...})` calls statically. See
[reference/validation.md](../reference/validation.md).
