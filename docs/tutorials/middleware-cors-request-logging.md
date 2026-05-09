# Add Middleware, CORS, Request IDs, And Request Logging

This path is useful when you are working in the app-host supported surface.
Compiler source-input support exists for the static subset documented in
[Framework metadata](../reference/framework.md); unsupported dynamic shapes fail
closed.

## Create

```sh
sloppy create observed-api --template minimal-api
cd observed-api
```

## Add Request IDs

```ts
import { Sloppy, Results, RequestId, RequestLogging } from "sloppy";

const app = Sloppy.create();

app.use(RequestId.defaults());
app.use(RequestLogging.defaults());
```

`RequestId.defaults()` assigns `ctx.requestId` and writes an `x-request-id`
response header. `RequestLogging.defaults()` records request metadata without
logging request bodies.

## Add CORS

```ts
app.useCors({
  origins: ["https://app.example"],
  headers: ["content-type"],
  exposedHeaders: ["x-request-id"],
  credentials: true,
});
```

The app-host creates preflight routes for CORS-enabled patterns.

## Add A Route

```ts
app.get("/health", () => Results.text("ok"));

export default app;
```

## Verify

```sh
sloppy build
sloppy run --once GET /health
```

Expected result: a V8-enabled runtime returns `ok`. If the source shape is
outside the compiler subset, `sloppy build` reports the unsupported construct
with a stable diagnostic.
