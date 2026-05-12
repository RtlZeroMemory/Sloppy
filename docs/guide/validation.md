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

In compiled/native runs, schema-backed JSON request metadata also enables the
native validator. Invalid JSON, missing fields, type mismatches, string/number
bounds, array length bounds, and rejected unknown fields fail before the handler
runs. Generated wrappers omit duplicate schema validation and materialize a
JavaScript body object through the existing JSON helper once when handler code
needs it; native body slot/projection handoff is future work. Dynamic response
shapes with `.returns(...)` remain metadata or fallback unless the compiler can
prove a static native JSON response.

The lowercase `schema` export remains available for existing code. New code
should use `Schema` so request/response metadata reads like a first-party API.
