# Middleware

Middleware wraps every handler in a route. Use it for auth checks, request
logging, response headers, or anything that runs around the handler.

```ts
import { Sloppy, Results } from "sloppy";

const app = Sloppy.create();

app.use(async (ctx, next) => {
    const start = Date.now();
    const result = await next();
    ctx.log.info(`${ctx.request.method} ${ctx.request.path} ${Date.now() - start}ms`);
    return result;
});

app.get("/", () => Results.text("ok"));
```

A middleware is `(ctx, next) => Result | Promise<Result>`. Call `next()` to
invoke the rest of the pipeline (further middleware then the handler), or
short-circuit by returning a `Results.*` value without calling it.

## Registering

| API                     | Scope                                                  |
| ----------------------- | ------------------------------------------------------ |
| `app.use(fn)`           | Every route registered after this call                 |
| `group.use(fn)`         | Every route registered on this group                   |

`app.use` and `group.use` accept any function. `app.use` distinguishes
middleware from provider descriptors and worker resources by argument shape —
plain functions are middleware.

Middleware runs in the order it was registered. A `group.use` registered after
an `app.use` runs after that app middleware; a later `app.use` runs after
earlier `group.use` calls. Registration order is the contract.

## Calling `next()`

```ts
app.use(async (ctx, next) => {
    if (!ctx.request.headers.get("authorization")) {
        return Results.problem({ status: 401, title: "Unauthorized" });
    }
    return await next();
});
```

- Return the value of `next()` (or `await next()`) to continue.
- Return any other `Results.*` value (without `next()`) to short-circuit.
- Calling `next()` more than once throws.
- A middleware that calls `next()` and then ignores its return value is
  guarded: the pipeline waits for the downstream chain to settle before the
  request scope is disposed.

## Async behavior

Middleware can be sync or `async`. `next()` always returns a Promise — even
when downstream is synchronous — so `await next()` is the idiomatic shape.

The app-host-created request service scope is disposed after the final
middleware or handler result settles. Don't return early without chaining
`next()` if downstream work has side effects you need to wait for.

## Inspecting routes

Each route snapshot exposes `metadata.middleware.count` for tooling:

```ts
const routes = app.__getRoutes();
console.log(routes[0].metadata.middleware.count);
```

## Status

Middleware runs through the app-host handler path. The compiler accepts
top-level `app.use(fn)` and `group.use(fn)` registrations when the middleware
is an inline or top-level function with a static shape. Emitted artifacts
include generated middleware wrappers and Plan metadata. Dynamic middleware
lookup and unsupported captures are rejected at build time with
`SLOPPYC_E_UNSUPPORTED_MIDDLEWARE`.
