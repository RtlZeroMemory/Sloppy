# Request logging

Sloppy's app-host can assign a request ID and write one structured log entry for
each completed request.

```ts
import { Sloppy, Results, RequestId, RequestLogging } from "sloppy";

const app = Sloppy.create();

app.use(RequestId.defaults());
app.use(RequestLogging.defaults());

app.get("/hello", (ctx) =>
    Results.json({ requestId: ctx.requestId }));

export default app;
```

`RequestId.defaults()` sets `ctx.requestId` before later middleware and handlers
run. It also writes `x-request-id` on the response by default.

Use an explicit generator in tests:

```ts
app.use(RequestId.defaults({
    generator: () => "req-test-1",
}));
```

Use a trusted incoming ID only at a boundary where the caller is allowed to set it:

```ts
app.use(RequestId.defaults({
    header: "x-request-id",
    trustIncoming: true,
}));
```

Trusted values are accepted only when they are safe HTTP header values. Unsafe or
empty values are ignored and the middleware generates a new ID.

`RequestLogging.defaults()` writes through `ctx.log.info(...)` with the message
`request completed`. Source-input builds support the static
`RequestLogging.defaults(...)` subset; dynamic option values fail closed. In
native `sloppy run` handlers, direct `ctx.log` calls write into the native
logging queue and configured native sinks.

The default fields are:

- `method`
- `path`
- `status`
- `route`
- `requestId`
- `durationMs`

Customize the small field set when a test or local diagnostic needs less data:

```ts
app.use(RequestLogging.defaults({
    includeRoute: true,
    includeDuration: false,
    includeRequestId: true,
}));
```

Attach a memory sink in tests:

```ts
const builder = Sloppy.createBuilder();
const sink = builder.logging.addMemorySink();
const app = builder.build();

app.use(RequestId.defaults({ generator: () => "req-1" }));
app.use(RequestLogging.defaults({ includeDuration: false }));
app.get("/", () => Results.text("ok"));

await app.__getRoutes()[0].handler();

sink.entries()[0].fields.requestId; // "req-1"
```

Native request contexts also expose `ctx.requestId`, `ctx.routeName`, and
`ctx.routePattern` for direct handler logging. `ctx.route` remains route
parameters:

```ts
app.get("/users/{id:int}", (ctx) => {
    ctx.log.forCategory("users").info("user fetched", {
        userId: ctx.route.id,
        requestId: ctx.requestId,
        route: ctx.routePattern,
    });
    return Results.json({ id: ctx.route.id });
});
```

Current request logging records one structured metadata entry per app-host
request. Tracing and metrics are separate observability features. Native
console and JSONL file sinks are available through `sloppy run` config; the
memory sink remains the deterministic test sink.
