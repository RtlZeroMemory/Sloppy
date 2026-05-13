# Realtime Updates

Use realtime routes when you want Plan-visible endpoints for event streams or
WebSocket upgrades. The current alpha supports SSE route registration,
metadata, and native HTTP/1.1 WebSocket Upgrade for V8-backed `sloppy run`
routes.

## Add An SSE Route

```ts
import { Sloppy } from "sloppy";

const app = Sloppy.create();

app.sse("/events", async (ctx, stream) => {
    stream.event("ready", {
        path: ctx.request.path,
        route: ctx.routePattern,
    });
    stream.heartbeat();
});

export default app;
```

Build and inspect the route:

```powershell
sloppy build app.js --out .sloppy
sloppy routes .sloppy
```

The route is still a `GET` route, but `sloppy routes` marks it with kind `sse`.
OpenAPI output also carries Sloppy realtime extensions so downstream tooling can
distinguish it from a normal JSON endpoint.

## Protect Realtime Routes

Realtime routes use the same auth hooks as ordinary routes:

```ts
app.group("/live")
    .requireAuth({ role: "admin" })
    .sse("/events", async (ctx, stream) => {
        stream.event("admin-ready", { user: ctx.user.name });
    });
```

Auth runs before the realtime handler. A rejected request returns the configured
auth failure response instead of an SSE/WebSocket response.

## Model A WebSocket Endpoint

```ts
app.ws("/socket", async (ctx, socket) => {
    await socket.sendJson({ ok: true });
});
```

This records WebSocket route intent in the Plan and OpenAPI output. Native
Upgrade execution accepts valid HTTP/1.1 WebSocket handshakes and delivers text
and binary messages to V8. Direct non-Upgrade HTTP calls fail because the route
requires an Upgrade request.

## Use A Hub In Tests

`Realtime.hub(name)` is an in-process helper for deterministic app-host tests:

```ts
import { Realtime } from "sloppy";

const hub = Realtime.hub("notifications");
const alice = hub.register("alice");

alice.join("team");
await hub.group("team").sendJson({ type: "refresh" });
```

Do not treat hubs as a production broker. They are not shared between processes
and are not connected to WebSocket upgrades in this alpha.

## Check The Boundaries

- Use SSE for current route/API shape work.
- Use TestHost for protected WebSocket route coverage; native protected
  WebSocket routes fail closed until auth principals are materialized on
  upgraded connections.
- Do not claim browser push, socket backpressure, heartbeat timers, replay, or
  cross-node fan-out.
- Use [`Realtime API`](../api/realtime.md) for exact method and option details.
