# Results

A handler returns a result descriptor. The runtime serializes that descriptor
into an HTTP response.

```ts
import { Results } from "sloppy";

app.get("/", () => Results.text("ok"));
app.get("/users", () => Results.ok([{ id: 1 }]));
app.get("/oops", () => Results.problem({ status: 500, title: "Boom" }));
```

Every helper is on the `Results` namespace.

## JSON results

| Helper                              | Status | `Content-Type`                  |
| ----------------------------------- | ------ | ------------------------------- |
| `Results.ok(value, options?)`       | 200    | `application/json; charset=utf-8` |
| `Results.created(loc, value, opts?)`| 201    | `application/json; charset=utf-8`, `Location: <loc>` |
| `Results.accepted(value, options?)` | 202    | `application/json; charset=utf-8` |
| `Results.notFound(value?, options?)`| 404    | `application/json; charset=utf-8` |
| `Results.badRequest(value?, opts?)` | 400    | `application/json; charset=utf-8` |

```ts
return Results.ok({ id: 1, name: "Ada" });
return Results.created(`/users/${user.id}`, user);
```

`Results.notFound()` and `Results.badRequest()` accept an optional payload.
Called with no argument, the descriptor carries no body — the runtime
sends only headers. With a string they emit `"…"`; with an object, the
object.

## Body-shape results

| Helper                          | Status | `Content-Type`                  |
| ------------------------------- | ------ | ------------------------------- |
| `Results.text(body, options?)`  | 200    | `text/plain; charset=utf-8`     |
| `Results.json(value, options?)` | 200    | `application/json; charset=utf-8` |
| `Results.html(body, options?)`  | 200    | `text/html; charset=utf-8`      |
| `Results.bytes(body, options?)` | 200    | `application/octet-stream`      |

`body` for `text` and `html` is a string. For `bytes`, it's a `Uint8Array`
(or any `BufferSource`).

## No-content

```ts
return Results.noContent();   // 204
```

## Custom status

```ts
return Results.status(202, { jobId: "x" });
return Results.status(418);                    // empty body
```

`Results.status(code, value?, options?)` is the escape hatch for any status
code. With a value, it serializes as JSON.

## Problem details

```ts
return Results.problem({
    status: 409,
    title: "User already exists",
    detail: "A user with that email is already registered.",
    code: "USER_ALREADY_EXISTS",
});
```

Returns `application/problem+json; charset=utf-8` with the supplied
fields. A bare string becomes the `title` of a `500` (with no `detail`
unless you pass an object):

```ts
return Results.problem("Database is down");
// → { title: "Database is down", status: 500 }
```

Calling `Results.problem()` with no argument produces
`{ title: "Sloppy problem", status: 500 }`.

### Default error responses

Install `ProblemDetails.defaults()` once on the app to wrap every route in a
catch that returns a safe `500 application/problem+json` body whenever a
handler throws or rejects:

```ts
import { Sloppy, ProblemDetails, Results } from "sloppy";

const app = Sloppy.create();
app.use(ProblemDetails.defaults());

app.get("/boom", () => { throw new Error("internal failure"); });
```

The thrown `/boom` handler returns:

```json
{"status":500,"title":"Internal Server Error","code":"SLOPPY_E_HANDLER_ERROR"}
```

The default body never includes the exception message. Pass
`ProblemDetails.defaults({ detail: "development" })` to include it only when
`Sloppy:Environment` is `Development`, or `{ detail: "always" }` to include it
in every environment. Explicit `Results.problem(...)` descriptors are passed
through untouched.

## Options

Every helper takes a final options object:

```ts
return Results.ok(data, {
    status: 207,                        // override the default status
    headers: { "x-trace": traceId },    // extra response headers
    contentType: "application/vnd.foo+json",
});
```

- `status` — override the helper's default status code.
- `headers` — added to the response. Header names are normalized to
  lowercase; later writes override earlier ones.
- `contentType` — override the helper's default `Content-Type`. The runtime
  still adds `; charset=utf-8` for textual content where appropriate.

## Async handlers

Handlers can return a `Promise<Result>`. The runtime awaits it during the
owner-thread microtask drain — long-running awaits aren't supported yet.

```ts
app.get("/users/{id:int}", async (ctx) => {
    const user = await loadUser(ctx.route.id);
    return user ? Results.ok(user) : Results.notFound();
});
```

If the promise rejects or the handler throws, the runtime returns `500`
with a redacted diagnostic body.
