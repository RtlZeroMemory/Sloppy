# Request context

A handler receives a single argument, conventionally named `ctx`:

```ts
app.get("/users/{id:int}", (ctx) => {
    return Results.json({ id: ctx.route.id });
});
```

`ctx` carries everything Sloppy knows about the current request, plus
the app-level objects you'll usually need.

## Shape

| Field              | Type                  | Notes                                              |
| ------------------ | --------------------- | -------------------------------------------------- |
| `ctx.route`        | object                | Route parameters                                   |
| `ctx.query`        | object                | Decoded query parameters as strings                |
| `ctx.request`      | `RequestInfo`         | Method, path, headers, body helpers                |
| `ctx.signal`       | cancellation signal   | `aborted` flag plus `throwIfAborted()`             |
| `ctx.deadline`     | deadline              | Per-request deadline metadata                      |
| `ctx.routeName`    | string                | Matched route name, when known                     |
| `ctx.routePattern` | string                | Matched route pattern, when known                  |
| `ctx.services`     | service scope         | Per-request DI scope                               |
| `ctx.config`       | `ConfigProvider`      | App config (read-only)                             |
| `ctx.log`          | `Logger`              | Request logger                                     |
| `ctx.capabilities` | capability provider   | Capabilities declared at build time                |

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

JSON bodies must declare `application/json` or `application/*+json`.

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

A scoped service resolver. Each request gets its own scope; scoped
services are constructed once per request and disposed when the
request ends.

```ts
app.get("/users/{id:int}", (ctx) => {
    const repo = ctx.services.get("users.repo");
    return Results.json(repo.find(ctx.route.id));
});
```

See [services](services.md) for lifetimes, disposal, and resolution
rules.

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

These mirror `app.config`, `app.log`, and `app.capabilities` — the same
objects you registered on the builder.

```ts
app.get("/", (ctx) => {
    ctx.log.info("hit /");
    return Results.text(ctx.config.getString("app:greeting", "hi"));
});
```

`ctx.log` supports `trace`, `debug`, `info`, `warn`, `error`, `isEnabled`, and
`forCategory`. In native runs, `ctx.log` writes to the native logging queue and
configured sinks. See [logging](logging.md).

## Not yet

- A streaming response writer.
- Multipart / form-data and file uploads.
- Cookies as a first-class field.
