# Add Config, Services, And Logging

This tutorial uses the current app-host/stdlib shape. Keep values static and
literal when you want the compiler to extract metadata.

## Start From A Full API

```sh
sloppy create config-api --template full-api
cd config-api
```

## Add Configuration

Add a value to `appsettings.json`:

```json
{
  "Greeting": "hello"
}
```

Read it in a handler through `ctx.config`:

```ts
app.get("/message/{name}", (ctx) => {
  const greeting = ctx.config.getString("Greeting", "hello");
  return Results.json({ message: `${greeting}, ${ctx.route.name}` });
});
```

## Add A Service

Register services before routes that use them:

```ts
const builder = Sloppy.createBuilder();

builder.services.addSingleton("Clock", () => ({
  now: () => "2026-05-09T00:00:00Z",
}));

const app = builder.build();
```

Handlers receive the request service provider through `ctx.services`.

## Log A Request Event

```ts
app.get("/message/{name}", (ctx) => {
  ctx.log.info("message route handled", { route: "/message/{name}" });
  return Results.json({ message: ctx.route.name });
});
```

Sloppy logging is structured and avoids request bodies by default.

## Verify

```sh
sloppy build
sloppy doctor .sloppy
sloppy run .sloppy --once GET /message/Ada
```

Expected result: `doctor` reports Plan health, and a V8-enabled runtime returns
a JSON response for `/message/Ada`.
