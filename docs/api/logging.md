# Logging

Every Sloppy app gets a logger. Use `app.log` or `ctx.log` from a handler.

```ts
app.get("/", (ctx) => {
    ctx.log.info("served root");
    return Results.text("ok");
});
```

## Levels

Five levels, lowest to highest:

```
trace · debug · info · warn · error
```

```ts
ctx.log.trace("entering");
ctx.log.debug("loaded user", { id });
ctx.log.info("served");
ctx.log.warn("retrying", { attempt: 2 });
ctx.log.error("failed", { error: String(err) });
```

Each call accepts a message string and an optional fields object. Fields
should be JSON-serializable.

The minimum level is set on the builder:

```ts
builder.logging.setMinimumLevel("info");
```

Default is `info`. Anything below the minimum is dropped.

## Memory sink

For tests and inspection, `addMemorySink()` returns a sink that retains every
emitted entry:

```ts
const builder = Sloppy.createBuilder();
const sink = builder.logging.addMemorySink();

const app = builder.build();
app.log.info("hello");

sink.entries();
// [{ level: "info", message: "hello", fields: undefined }]
```

`sink.entries()` returns a frozen array. Useful in tests:

```ts
expect(sink.entries().some((e) => e.level === "error")).toBe(false);
```

## Console output

The runtime wires up a default JSON console sink when an app is served via
the `sloppy run` HTTP path. You don't need to call anything to see logs.

> Experimental: the API for adding console or file sinks programmatically is
> in flux. The runtime sink is enough for now; add a memory sink in tests.

## Structured fields

Fields are merged into the structured log line, not interpolated into the
message. Prefer fields over string concatenation:

```ts
// good
ctx.log.info("user fetched", { userId: id, durationMs: dt });

// less searchable
ctx.log.info(`user ${id} fetched in ${dt}ms`);
```

## Log levels in config

Set the minimum level via configuration if you want to flip it without code
changes:

```ts
builder.config.addObject({ "logging:minimumLevel": "debug" });
builder.logging.setMinimumLevel(
    builder.config.getString("logging:minimumLevel", "info"),
);
```
