# Validate JSON requests

Use Sloppy `Schema` values for JSON request bodies that need framework-owned
validation and OpenAPI metadata.

```ts
import { Results, Schema } from "sloppy";

const CreateUser = Schema.object({
    name: Schema.string().min(1).max(100),
    email: Schema.string().email(),
});

const User = Schema.object({
    id: Schema.integer(),
    name: Schema.string(),
    email: Schema.string(),
});

app.post("/users", async (ctx) => {
    const input = await ctx.body.validate(CreateUser);
    const user = await users.create(input);
    return Results.created(`/users/${user.id}`, user);
})
    .accepts(CreateUser)
    .returns(User);
```

`ctx.body.validate(schema)` parses the JSON body and returns the validated
value. Invalid JSON, missing required fields, wrong field types, invalid email
values, and nested array element failures return `400 application/problem+json`.
Validation problem entries include the failing path, a code, and a message.
They do not copy raw body values into the response.

`.accepts(schema)` records request body metadata. `.returns(schema)` records
the default JSON response body metadata. Static schema identifiers are emitted
to the compiler Plan and consumed by `sloppy openapi`.

The lowercase `schema` export remains available for existing code. New code
should use `Schema` so request/response metadata reads like a first-party API.
