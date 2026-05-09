# Logging

Every Sloppy app has a structured logger. Use `app.log` for application-level
events and `ctx.log` inside handlers.

```ts
app.get("/users/{id:int}", (ctx) => {
    ctx.log.info("user fetched", {
        userId: ctx.route.id,
        requestId: ctx.requestId,
    });
    return Results.ok({ id: ctx.route.id });
});
```

## Levels

Levels, lowest to highest:

```text
trace
debug
info
warn
error
off
```

```ts
ctx.log.trace("entering");
ctx.log.debug("loaded user", { id });
ctx.log.info("served");
ctx.log.warn("retrying", { attempt: 2 });
ctx.log.error("failed", { error: String(err) });
```

Set the minimum level on the builder when you are using the bootstrap app-host:

```ts
builder.logging.setMinimumLevel("info");
```

Use `isEnabled(level)` around expensive field construction:

```ts
if (ctx.log.isEnabled("debug")) {
    ctx.log.debug("query plan", { plan: describePlan(query) });
}
```

The default minimum level is `info`. Events below the minimum level are dropped
before field conversion and sink dispatch.

## Categories

`forCategory(name)` returns a logger that writes the same request metadata with a
different category.

```ts
const usersLog = ctx.log.forCategory("users");
usersLog.info("profile loaded", { userId: ctx.route.id });
```

Request-context loggers use the `request` category by default. `app.log` uses the
application logger category used by the current host surface.

## Fields

Each log method accepts a message and an optional shallow fields object:

```ts
ctx.log.info("payment accepted", {
    orderId,
    amountCents: 1299,
    saved: true,
});
```

Supported field values are `null`, booleans, finite numbers, and strings. Field
objects are shallow and bounded; arrays, functions, nested objects, symbols, and
other unsupported values are rejected instead of being stringified implicitly.

Native events currently retain up to eight fields. Field names and values are
copied into fixed-size event storage before the event enters the queue.

## Redaction

Redaction runs before sinks receive an event. Sensitive keys are matched
case-insensitively, including common dotted or camel-case variants:

- `password`, `passwd`, `pwd`
- `secret`, `token`
- `authorization`, `cookie`, `set-cookie`
- `apiKey`, `api_key`
- `clientSecret`, `client_secret`
- `privateKey`, `private_key`
- `passphrase`
- `connectionString`, `connection_string`

Add app-specific keys in the bootstrap builder:

```ts
builder.logging.addRedactionKey("tenantSecret");
```

Redacted fields keep their key and replace the value with `[REDACTED]`.

## Sinks

The bootstrap builder supports a memory sink for tests:

```ts
const builder = Sloppy.createBuilder();
const sink = builder.logging.addMemorySink({ capacity: 32 });

const app = builder.build();
app.log.info("hello");

sink.entries();
```

It also records console and file sink descriptors:

```ts
builder.logging
    .setMinimumLevel("info")
    .setQueueCapacity(64)
    .writeTo.console({ format: "pretty" })
    .writeTo.file({ path: "app.jsonl", format: "jsonl" });
```

In the native `sloppy run` path, logging sinks are created from Plan/config
metadata. Console logging is enabled by default and writes pretty output to
stderr. File logging writes JSONL in append mode with buffered writes and flushes
on shutdown.

```json5
{
  "logging": {
    "minimumLevel": "debug",
    "queueCapacity": 64,
    "console": {
      "enabled": true,
      "format": "jsonl"
    },
    "file": {
      "path": "app.jsonl",
      "format": "jsonl"
    }
  }
}
```

Relative file paths are resolved by the current `sloppy run` artifact path. The
parent directory must already exist.

## Request Logging

Use `RequestLogging.defaults()` to write one structured entry per completed
app-host request. Install `RequestId.defaults()` first when you want the entry to
include a middleware-provided request ID:

```ts
import { RequestId, RequestLogging } from "sloppy";

app.use(RequestId.defaults());
app.use(RequestLogging.defaults());
```

The default entry includes method, path or target, status, route pattern when
known, request ID when present, and `durationMs`. Request bodies and request
headers are not logged by default.

Native request contexts also expose `ctx.requestId`, `ctx.routeName`,
`ctx.routePattern`, and `ctx.log`; events written through `ctx.log` fan out to
the configured native sinks. `ctx.route` remains the route-parameter object.
