# Request context

A handler receives a single argument, conventionally named `ctx`:

```ts
app.get("/users/{id:int}", (ctx) => {
    return Results.json({ id: ctx.route.id });
});
```

`ctx` carries everything Sloppy knows about the current request, plus the
app-level objects you'll usually need.

## Shape

| Field             | Type                  | Notes                                              |
| ----------------- | --------------------- | -------------------------------------------------- |
| `ctx.route`       | object                | Route parameters as strings                        |
| `ctx.query`       | object                | Decoded query parameters as strings                |
| `ctx.request`     | `RequestInfo`         | Method, path, headers, body helpers                |
| `ctx.services`    | `ServiceScope`        | Per-request DI scope                               |
| `ctx.config`      | `ConfigProvider`      | App config (read-only)                             |
| `ctx.log`         | `Logger`              | Request logger                                     |
| `ctx.capabilities`| `CapabilityProvider`  | Capabilities declared at build time                |

## `ctx.route`

Each entry corresponds to a `{name}` or `{name:int}` parameter in the route
pattern. Values are always strings, even when the type tag is `:int`.

```ts
app.get("/users/{id:int}/comments/{slug}", (ctx) => {
    const id = Number(ctx.route.id);    // string -> number
    const slug = ctx.route.slug;        // string
    return Results.json({ id, slug });
});
```

## `ctx.query`

Decoded query parameters keyed by name. Repeated keys take last-wins:

```
GET /search?q=hello&q=world
ctx.query.q === "world"
```

Plus signs decode to spaces; `%XX` percent-escapes are decoded. Invalid
escapes fail before the handler runs. Bodies, files, or arrays of values
aren't surfaced today — use the body for structured input.

## `ctx.request`

| Member                       | Type        | Notes                                          |
| ---------------------------- | ----------- | ---------------------------------------------- |
| `request.method`             | string      | `"GET"`, `"POST"`, etc.                        |
| `request.path`               | string      | Decoded path                                   |
| `request.rawTarget`          | string      | The raw request target including query string  |
| `request.headers.get(name)`  | string?     | Case-insensitive header lookup, comma-joined   |
| `request.headers.entries()`  | iterable    | Deterministic header list                      |
| `request.text()`             | `Promise<string>`  | Body as text (for `text/plain`)         |
| `request.json()`             | `Promise<unknown>` | Parsed JSON body                        |

`text()` and `json()` are async even when the body is already in memory.
JSON bodies must declare `application/json` or `application/*+json`.

The runtime rejects malformed bodies, oversized bodies, unsupported transfer
encodings, and unsupported media types before the handler runs:

| Status | Cause                                                          |
| ------ | -------------------------------------------------------------- |
| 400    | Malformed JSON                                                 |
| 413    | Body exceeded the configured limit                             |
| 415    | Unsupported `Content-Type`                                     |
| 501    | Transfer encoding the runtime doesn't accept                   |

Handler exceptions and unsupported result descriptors return `500`.

## `ctx.services`

A scoped service resolver. Each request gets its own scope; scoped services
are constructed once per request and disposed when the request ends.

```ts
app.get("/users/{id:int}", (ctx) => {
    const repo = ctx.services.get("users.repo");
    return Results.json(repo.find(ctx.route.id));
});
```

See [services](services.md) for lifetimes, disposal, and resolution rules.

## `ctx.config`, `ctx.log`, `ctx.capabilities`

These mirror `app.config`, `app.log`, and `app.capabilities` — the same
objects you registered on the builder. Use them inside handlers when you
prefer reading config or logging directly:

```ts
app.get("/", (ctx) => {
    ctx.log.info("hit /");
    return Results.text(ctx.config.getString("app:greeting", "hi"));
});
```

## What isn't on `ctx` yet

- `ctx.signal` / `ctx.deadline` — request cancellation surfaces are still
  experimental in the bridge; for now, deadlines flow through the data API
  via `{ signal, deadline, timeoutMs }` options. See [data](data.md).
- A streaming response writer.
- Cookies as a first-class field.
