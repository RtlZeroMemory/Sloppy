# Routing

A Sloppy app dispatches requests by matching the request method and path
against a route table built from your registrations.

```ts
app.get("/users/{id:int}", (ctx) =>
    Results.json({ id: ctx.route.id })
);
```

## Verb methods

```ts
app.get(pattern, handler)
app.post(pattern, handler)
app.put(pattern, handler)
app.patch(pattern, handler)
app.delete(pattern, handler)
```

`mapGet`, `mapPost`, `mapPut`, `mapPatch`, and `mapDelete` are aliases — same
behavior, longer name.

`HEAD` and `OPTIONS` are reserved by the framework. The runtime answers
`OPTIONS` for allowed-method/preflight responses. You can't register a
handler for them today.

## Route patterns

Patterns must start with `/`. Path segments are either literal text or a
parameter:

| Syntax       | Matches                                  | `ctx.route.*`                |
| ------------ | ---------------------------------------- | ---------------------------- |
| `/users`     | `/users` exactly                         | —                            |
| `{name}`     | a single non-`/` segment                 | `ctx.route.name` (string)    |
| `{id:int}`   | a single segment of digits               | `ctx.route.id` (string)      |

Even with `:int`, `ctx.route.id` is a string — the type tag validates the
segment but doesn't coerce. Convert it yourself if you need a number:

```ts
app.get("/users/{id:int}", (ctx) => {
    const id = Number(ctx.route.id);
    return Results.json({ id });
});
```

Trailing slashes are strict. `/users` and `/users/` are different routes.

When several routes could match, Sloppy prefers literal segments over
parameter segments and falls back to source order.

## Options object

Route methods accept an options object as the second argument:

```ts
app.get("/users", { name: "Users.List", tags: ["users"] }, handler);
```

The options accepted today are `name` and `tags`. They show up in
`sloppy routes` output and in OpenAPI metadata.

The fluent form is equivalent:

```ts
app.get("/users", handler).withName("Users.List");
```

`withName(...)` returns the same registration object so you can chain.

## Route groups

`app.group("/prefix")` returns a group object with the same verb methods. Use
it when several routes share a prefix or a tag.

```ts
const users = app.group("/users").withTags("users");

users.get("/", listUsers);
users.get("/{id:int}", getUser);
users.post("/", createUser);
```

Group prefixes normalize trailing slashes (`"/users"` plus `"/{id:int}"`
becomes `"/users/{id:int}"`). Child patterns may start with `/` or be
relative. A child pattern of `/` maps to the group prefix itself.

Groups can nest:

```ts
const v1 = app.group("/v1");
const users = v1.group("/users");
users.get("/", listUsers); // → /v1/users
```

Group tags merge with route tags. Group names propagate to route metadata.

## Controllers

A controller is a class whose methods become route handlers. Use them when
you want to share state or services across several routes.

```ts
class UsersController {
    static inject = ["users.repo"];

    constructor(repo) {
        this.repo = repo;
    }

    list(ctx) {
        return Results.json(this.repo.all());
    }

    get(ctx) {
        return Results.json(this.repo.find(ctx.route.id));
    }
}

app.controller("/users", UsersController, (users) => {
    users.get("/", "list").withName("Users.List");
    users.get("/{id:int}", "get").withName("Users.Get");
});
```

The `static inject` array lists service tokens to pass to the constructor.
Inside the configure callback, `users.get("/path", "methodName")` maps a
controller method to a route. `app.mapController` is an alias.

## Handler shape

A handler is a function (or async function) that takes `ctx` and returns a
result descriptor:

```ts
app.get("/", (ctx) => Results.text("hi"));
app.get("/json", async (ctx) => Results.json({ ok: true }));
```

Handlers can return either a plain `Results.*` value or a `Promise` of one.
Returning anything else fails the request with a runtime diagnostic.

What's on `ctx` is documented in [request-context](request-context.md).

## What's not supported yet

- Middleware and per-route filters (planned: see the framework capability pack)
- Streaming or chunked request bodies
- `multipart/form-data` and file uploads
- Custom matchers beyond `{name}` / `{name:int}`
- Per-route limits at the API surface (server-wide limits exist via config)
