# Routing

A Sloppy app dispatches requests by matching the request method and path
against a route table built from your registrations.

Routing is transport-neutral. The same route table and handler model are used
for HTTP/1.1 and HTTP/2 requests; HTTP/2 pseudo-headers are translated into the
same method, target, host, headers, and body shape before dispatch.

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
app.sse(pattern, handler)
app.ws(pattern, handler)
```

`mapGet`, `mapPost`, `mapPut`, `mapPatch`, and `mapDelete` are aliases — same
behavior, longer name.

`sse` and `ws` register `GET` routes with realtime metadata. `sse` wraps the
handler in the current bounded server-sent events stream shape. `ws` records
WebSocket route intent and returns the current `501` unavailable response until
native upgrade execution exists. See [Realtime](realtime.md).

`HEAD` and `OPTIONS` are not directly registrable verbs. Incoming `HEAD`
requests match the corresponding `GET` route and return the same headers
without response body bytes. `OPTIONS` is auto-installed by
[`app.useCors(policy)`](cors.md) for preflight; otherwise incoming `OPTIONS`
requests get `405 Method Not Allowed`. When the Plan-backed runtime can identify
the matched route path, a `405` response includes an `Allow` header with the
supported methods for that path. `GET` routes also advertise `HEAD`.

## Route patterns

Patterns must start with `/`. Path segments are either literal text or
a parameter:

| Syntax        | Matches                                  | `ctx.route.*`             |
| ------------- | ---------------------------------------- | ------------------------- |
| `/users`      | `/users` exactly                         | —                         |
| `{name}`      | a single non-`/` segment                 | `ctx.route.name` (string) |
| `{name:str}`  | same as `{name}`, explicit               | `ctx.route.name` (string) |
| `{id:int}`    | a single segment of digits               | `ctx.route.id` (string)   |

Even with `:int`, `ctx.route.id` is a string — the type tag validates
the segment but doesn't coerce. Convert it yourself if you need a
number:

```ts
app.get("/users/{id:int}", (ctx) => {
    const id = Number(ctx.route.id);
    return Results.json({ id });
});
```

Trailing slashes are strict. `/users` and `/users/` are different
routes.

When several patterns could match, the runtime sorts routes with no
parameters before routes with any parameter; ties break in source
order.

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
`accepts(schema)` records a JSON request body schema, and `returns(schema)`
records the default JSON response schema:

```ts
app.post("/users", createUser)
    .accepts(CreateUser)
    .returns(User)
    .withName("Users.Create");
```

The app host stores this metadata in route snapshots. The compiler also uses
static schema identifiers in these fluent calls for Plan and OpenAPI metadata.

Use `requireAuth(...)` to protect a route:

```ts
app.get("/admin", handler)
  .requireAuth({ role: "admin" });
```

Route auth requirements are public-alpha/experimental.

See [Auth](auth.md) for JWT bearer, API keys, roles, claims, and policies.

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
Groups can also require auth for every child route:

```ts
const internal = app.group("/internal").requireAuth();
internal.get("/status", handler);
```

Groups also expose `sse` and `ws`:

```ts
const live = app.group("/live").requireAuth();
live.sse("/events", handler);
live.ws("/socket", handler);
```

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

## Middleware

Wrap handlers with `app.use(fn)` (every later route) or `group.use(fn)`
(group-local). See [middleware](middleware.md).

## Static files

Alpha: `app.useStaticFiles(options)` is an experimental build-time API.

Use [`app.useStaticFiles(options)`](static-files.md) to expose a
project-relative directory as generated static `GET` routes. Static files are
captured at build/package time, not looked up dynamically per request.

## What's not supported yet

- Direct HEAD route registration
- Streaming request bodies exposed directly to handlers
- Custom matchers beyond `{name}` / `{name:int}`
- Per-route limits at the API surface (server-wide limits exist via config)
- Native WebSocket upgrade execution
