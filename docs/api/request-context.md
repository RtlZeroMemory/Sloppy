# Request context

A handler receives a single argument, conventionally named `ctx`:

```ts
app.get("/users/{id:int}", (ctx) => {
    return Results.json({ id: ctx.route.id });
});
```

`ctx` carries everything Sloppy knows about the current request. The
bootstrap app-host/test-host path adds app-level objects directly on the
context. The native/V8 run path exposes the request, route/query metadata,
request ID, logger, signal, and deadline; generated typed wrappers materialize
services and config arguments when the compiled handler declares them.

## Shape

| Field              | Type                  | Notes                                              |
| ------------------ | --------------------- | -------------------------------------------------- |
| `ctx.route`        | object                | Route parameters                                   |
| `ctx.query`        | object                | Decoded query parameters as strings                |
| `ctx.request`      | `RequestInfo`         | Method, path, headers, body helpers                |
| `ctx.cookies`      | cookie bag            | Request cookies, last value wins                   |
| `ctx.signal`       | cancellation signal   | `aborted` flag plus `throwIfAborted()`             |
| `ctx.deadline`     | deadline              | Per-request deadline metadata                      |
| `ctx.routeName`    | string                | Matched route name, when known                     |
| `ctx.routePattern` | string                | Matched route pattern, when known                  |
| `ctx.log`          | `Logger`              | Request logger                                     |
| `ctx.user`         | `AuthUser`            | Public-alpha/experimental auth user; anonymous by default and authenticated after JWT/API-key auth succeeds |
| `ctx.services`     | service scope         | App-host/test-host direct field; compiler-generated wrappers use request scopes for `Service<T>` |
| `ctx.config`       | `ConfigProvider`      | App-host/test-host direct field; compiled `Config<"KEY">` parameters are materialized by generated wrappers |
| `ctx.capabilities` | capability provider   | App-host/test-host direct field                    |

## `ctx.route`

Each entry corresponds to a `{name}` or `{name:int}` parameter in the
route pattern. Values are always strings, even when the type tag is
`:int`.

```ts
app.get("/users/{id:int}/comments/{slug}", (ctx) => {
    const id = Number(ctx.route.id);    // string -> number
    const slug = ctx.route.slug;        // string
    return Results.json({ id, slug });
});
```

`ctx.route` only contains route parameters. Route metadata uses separate
top-level fields so parameters named `name` or `pattern` keep their normal
meaning:

| Field | Type | Notes |
| --- | --- | --- |
| `ctx.routeName` | string | Route name when the Plan route has one, otherwise `""` |
| `ctx.routePattern` | string | Matched route pattern, for example `"/users/{id:int}"` |

## `ctx.query`

Decoded query parameters keyed by name. Repeated keys take last-wins:

```
GET /search?q=hello&q=world
ctx.query.q === "world"
```

Plus signs decode to spaces; `%XX` percent-escapes are decoded. Invalid
escapes fail before the handler runs. Arrays of values aren't surfaced
today — use the body for structured input.

## `ctx.request`

| Member                       | Type      | Notes                                            |
| ---------------------------- | --------- | ------------------------------------------------ |
| `request.method`             | string    | `"GET"`, `"POST"`, etc.                          |
| `request.path`               | string    | Decoded path                                     |
| `request.rawTarget`          | string    | The raw request target including query string    |
| `request.headers.get(name)`  | string?   | Case-insensitive header lookup, comma-joined     |
| `request.headers.entries()`  | iterable  | Deterministic header list                        |
| `request.text()`             | string    | Body as text (for `text/plain`); synchronous     |
| `request.json()`             | unknown   | Parsed JSON body; synchronous                    |
| `request.form()`             | form data | Parsed `application/x-www-form-urlencoded` body  |
| `request.multipart()`        | form data | Parsed `multipart/form-data` fields and files    |
| `request.bytes()`            | Uint8Array | Raw body bytes; synchronous                     |

The body helpers are synchronous — the runtime has the full body in
memory before the handler runs, so there's nothing to await:

```ts
app.post("/users", (ctx) => {
    const body = ctx.request.json();
    if (!body || typeof body.name !== "string") {
        return Results.badRequest({ error: "name required" });
    }
    return Results.created(`/users/${createUser(body.name)}`, body);
});
```

The app-host/test-host context also exposes `ctx.body` as a shortcut to
`ctx.request.body`. Use `ctx.body.validate(schema)` to parse and validate JSON
with Sloppy `Schema` values:

```ts
app.post("/users", async (ctx) => {
    const input = await ctx.body.validate(CreateUser);
    return Results.created("/users/1", input);
}).accepts(CreateUser);
```

Invalid JSON and schema failures produce `400 application/problem+json`
validation problems in the app host.

JSON bodies must declare `application/json` or `application/*+json`. URL-encoded
forms must declare `application/x-www-form-urlencoded`. Multipart bodies must
declare `multipart/form-data` with a `boundary` parameter.

`request.form()` returns an object with:

| Member | Notes |
| --- | --- |
| `form.get(name)` | Last value for the field, or `null` |
| `form.entries()` | Iterable `[name, value]` pairs in request order |
| `form.file(name)` | Last uploaded file for the field, or `null` |

`request.multipart()` returns the same shape. Text parts are available through
`get()` and uploaded files through `file()`:

```ts
app.post("/profile", (ctx) => {
    const form = ctx.request.multipart();
    const avatar = form.file("avatar");

    return Results.ok({
        displayName: form.get("displayName"),
        avatarName: avatar?.name ?? null,
        avatarBytes: avatar?.size ?? 0,
    });
});
```

Uploaded file objects expose `fieldName`, `name`, `contentType`, `size`,
`bytes()`, `text()`, and `saveTo(path)`. Bodies are still bounded and buffered
in memory before the handler runs.

## `ctx.cookies`

`ctx.cookies.get(name)` returns the decoded request cookie value or `null`:

```ts
app.get("/me", (ctx) => {
    const session = ctx.cookies.get("session");
    return session ? Results.ok({ session }) : Results.unauthorized();
});
```

Cookie names must be valid HTTP token names. Repeated cookie names use
last-wins lookup.

The runtime rejects malformed bodies, oversized bodies, unsupported
transfer encodings, and unsupported media types before the handler
runs:

| Status | Cause                                                  |
| ------ | ------------------------------------------------------ |
| 400    | Malformed JSON                                         |
| 413    | Body exceeded the configured limit                     |
| 415    | Unsupported `Content-Type`                             |
| 501    | Transfer encoding the runtime doesn't accept           |

Handler exceptions and unsupported result descriptors return `500`.

## `ctx.signal` and `ctx.deadline`

Cancellation and deadline surfaces. Pass them into provider/worker
calls to propagate cancellation:

```ts
app.get("/users", async (ctx) => {
    const db = ctx.services.get("data.main");
    return Results.ok(
        await db.query(
            sql`SELECT id, name FROM users`,
            { signal: ctx.signal, deadline: ctx.deadline },
        ),
    );
});
```

`ctx.signal` exposes `aborted` (boolean), `reason`, and
`throwIfAborted()`. `ctx.deadline` carries the per-request deadline
the runtime computed from server timeouts.

## `ctx.services`

In the bootstrap app-host/test-host path, `ctx.services` is a scoped service
resolver. Each request gets its own scope; scoped services are constructed once
per request and disposed when the request ends.

```ts
app.get("/users/{id:int}", (ctx) => {
    const repo = ctx.services.get("users.repo");
    return Results.json(repo.find(ctx.route.id));
});
```

See [services](services.md) for lifetimes, disposal, and resolution
rules.

In compiler source input, prefer typed `Service<T>` handler parameters for
compiled artifacts. The generated wrapper creates and disposes the request
scope around the handler call.

## `ctx.requestId`

Native request contexts include a request ID string generated by the runtime.
The JavaScript app-host test path exposes `ctx.requestId` when
`RequestId.defaults()` middleware has run before the handler.

```ts
app.get("/status", (ctx) => {
    ctx.log.info("status checked", { requestId: ctx.requestId });
    return Results.json({ requestId: ctx.requestId });
});
```

## `ctx.config`, `ctx.log`, `ctx.capabilities`

In the bootstrap app-host/test-host path, these mirror `app.config`, `app.log`,
and `app.capabilities` — the same objects you registered on the builder.

```ts
app.get("/", (ctx) => {
    ctx.log.info("hit /");
    return Results.text(ctx.config.getString("app:greeting", "hi"));
});
```

`ctx.log` supports `trace`, `debug`, `info`, `warn`, `error`, `isEnabled`, and
`forCategory`. In native runs, `ctx.log` writes to the native logging queue and
configured sinks. Native request contexts do not directly expose `ctx.config`,
`ctx.services`, or `ctx.capabilities`; use typed handler parameters when a
compiled artifact needs those values. See [logging](logging.md).

## Current limits

- Request bodies are bounded and buffered before the handler runs.
- Multipart parsing covers ordinary text fields and in-memory file parts; it is
  not a streaming upload parser.
- Cookie signing, encryption, and session storage are application concerns.
