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

`sse` and `ws` are experimental alpha helpers that register `GET` routes with
realtime metadata. `sse` wraps the handler in the current bounded server-sent
events stream shape. `ws` records WebSocket route intent and native `sloppy run`
enters that handler for valid HTTP/1.1 WebSocket Upgrade requests. The API
shape is unstable and may change. See [Realtime](realtime.md).

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
| `{id:uuid}`   | UUID text with canonical dashes          | `ctx.route.id` (string)   |
| `{slug:alpha}`| ASCII letters only                       | `ctx.route.slug` (string) |
| `{n:float}`   | decimal digits with one `.`              | `ctx.route.n` (string)    |

Even with `:int`, `ctx.route.id` is a string — the type tag validates
the segment but doesn't coerce. Convert it yourself if you need a
number:

```ts
app.get("/users/{id:int}", (ctx) => {
    const id = Number(ctx.route.id);
    return Results.json({ id });
});
```

Trailing slashes are strict. Non-root route patterns cannot end in `/`; a
request for `/users/` does not match a registered `/users` route.

When several patterns could match, Sloppy uses deterministic precedence:
literal segments beat parameter segments, constrained parameters beat
unconstrained parameters, longer/more-specific patterns beat shorter ones,
and source order breaks remaining ties. Duplicate `method + pattern`
registrations are rejected. Duplicate route names are rejected.

## Contract metadata

Route methods accept an options object as the second argument:

```ts
app.get("/users", { name: "Users.List", tags: ["users"] }, handler);
```

The options accepted today are `name`, `tags`, `summary`, `description`, and
`deprecated`. They show up in `sloppy routes` output, OpenAPI metadata, and
compiled/native execution metadata when the compiler can prove the route shape.

The fluent form is equivalent:

```ts
app.get("/users", handler).withName("Users.List");
```

Route builders return the same registration object so you can chain contract
metadata:

```ts
app.post("/users", createUser)
    .accepts(CreateUser)
    .returns(201, User, { description: "User created" })
    .returns(400, ProblemDetails)
    .name("Users.Create")
    .summary("Create user")
    .description("Creates a user account.")
    .tags("Users")
    .produces("application/json")
    .consumes("application/json");
```

Supported fluent contract methods are:

| Method | Purpose |
| ------ | ------- |
| `.name(operationId)` / `.withName(operationId)` | OpenAPI operation ID and route name |
| `.summary(text)` | Short operation summary |
| `.description(text)` | Longer operation description |
| `.tags(...tags)` / `.withTags(...tags)` | OpenAPI tags and route inspection tags |
| `.deprecated(reasonOrBool?)` | Marks the operation deprecated, with an optional reason |
| `.accepts(schema, options?)` | JSON request body schema and optional content metadata |
| `.returns(status, schema?, options?)` | Response status, schema, description, and content metadata |
| `.produces(mediaType)` / `.consumes(mediaType)` | Response/request media type hints |
| `.header(name, schema, options?)` | Header parameter contract |
| `.query(schemaOrObject, options?)` | Query parameter object contract |
| `.params(schemaOrObject, options?)` | Route parameter object contract |
| `.requireAuth(...)` / `.requiresAuth(...)` / `.security(...)` / `.authorize(policy)` | Route security metadata |
| `.openapi(object)` | Static JSON-compatible override for advanced OpenAPI fields |

The app host stores this metadata in route snapshots. The compiler also uses
static schema identifiers in these fluent calls for Plan and OpenAPI metadata.
For compiled/native runs, `.accepts(...)` can enable native schema-backed JSON
request validation before the handler boundary. Static JSON result routes can
carry native preencoded JSON response metadata. Supported `.returns(...)`
response schemas can use the bounded native JSON response writer; unsupported
JSON shapes are still visible as generic/fallback modes in
`sloppy routes --dispatch`.

Invalid contract metadata fails early. Status codes must be OpenAPI HTTP status
codes, media types must be valid `type/subtype` tokens, and compiler-visible
schema references must name declared Sloppy schemas. When metadata is dynamic,
Sloppy reports partial OpenAPI instead of guessing.

## URL generation

Named routes can generate application-relative URLs:

```ts
app.get("/users/{id:int}", { name: "Users.Get" }, getUser);

const href = app.urlFor("Users.Get", { id: 42 }, { tab: "profile" });
// "/users/42?tab=profile"
```

`ctx.urlFor(...)` is available inside handlers and uses the same rules.
Route parameters are encoded as path segments. Query keys and values are
encoded for the query string. URL generation rejects missing route parameters,
extra route parameters, unknown route names, and unnamed routes.

Use `requireAuth(...)` to protect a route:

```ts
app.get("/admin", handler)
  .requireAuth({ role: "admin" });
```

Route auth requirements are public alpha, pre-production behavior.

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

Groups also expose the experimental `sse` and `ws` helpers:

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

`app.useStaticFiles(options)` is a public alpha, pre-production build-time API.

Use [`app.useStaticFiles(options)`](static-files.md) to expose a
project-relative directory as generated static `GET` routes. Static files are
captured at build/package time, not looked up dynamically per request.

## What's not supported yet

- Direct HEAD route registration
- Streaming request bodies exposed directly to handlers
- Custom matchers beyond the documented `str`, `int`, `uuid`, `alpha`, and
  `float` path constraints
- Per-route limits at the API surface (server-wide limits exist via config)
- WebSocket fragmentation, compression, heartbeat timers, and protected native
  WebSocket auth principal materialization
