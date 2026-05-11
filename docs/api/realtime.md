# Realtime

`Realtime` is the experimental route surface for server-sent events, current
WebSocket route metadata, and in-process hub helpers.

```ts
import { Sloppy, Realtime } from "sloppy";

const app = Sloppy.create();

app.sse("/events", async (ctx, stream) => {
    stream.event("ready", { ok: true });
});
```

## Route Methods

`app.sse(pattern, handler)` registers a `GET` route with realtime kind `sse`.
The handler receives the normal request context plus an SSE stream:

```ts
app.sse("/events", async (ctx, stream) => {
    stream.comment("connected");
    stream.event("ready", { path: ctx.request.path });
    stream.heartbeat();
});
```

Route groups expose the same method:

```ts
const live = app.group("/live").requireAuth();
live.sse("/events", handler);
```

`app.ws(pattern, handler)` and `group.ws(pattern, handler)` register `GET`
routes with realtime kind `websocket`. In this alpha, the route metadata,
compiler lowering, OpenAPI extension, auth ordering, and unavailable runtime
response are implemented. Native HTTP/1.1 upgrade execution is not implemented:
the generated handler returns `501` with code
`SLOPPY_E_REALTIME_WEBSOCKET_UNAVAILABLE`.

## `Realtime.sse(handler, options?)`

Wraps a handler in a `Results.stream(...)` descriptor with:

- `Content-Type: text/event-stream`
- `Cache-Control: no-cache`
- `X-Slop-Realtime: sse`

`options.maxQueuedEvents` bounds queued frame writes for one handler invocation.
The default is `64`. The current runtime still collects bounded stream chunks
before response serialization; this is an API and metadata shape, not a
production push transport with socket backpressure.

## SSE Stream

The second handler argument has these methods:

| Method | Behavior |
| --- | --- |
| `stream.send(data)` | Writes a default SSE `data:` frame. Non-string values are JSON-serialized. |
| `stream.event(name, data, options?)` | Writes a named event. Event names must be non-empty token strings. |
| `stream.comment(text)` | Writes an SSE comment frame. CR and LF are rejected. |
| `stream.heartbeat()` | Writes `: heartbeat`. |
| `stream.close()` | Closes the stream descriptor. |

Event options:

| Option | Behavior |
| --- | --- |
| `id` | Writes an `id:` field. CR and LF are rejected. |
| `retry` | Writes a non-negative integer retry value. |
| `comment` | Writes a comment before the event fields. CR and LF are rejected. |

## `Realtime.websocket(handler)`

Wraps a handler in the current unavailable WebSocket response. Use
`app.ws(...)` for route registration so the Plan records the route kind and
tooling can report it.

## `Realtime.hub(name)`

Creates an in-process hub object for deterministic bootstrap tests and app-host
fixtures:

```ts
const hub = Realtime.hub("notifications");
const client = hub.register("user:1");

client.join("admins");
await hub.group("admins").sendJson({ type: "refresh" });
```

The hub stores connection state in memory. It is not a broker, not shared
across processes, and not wired to native WebSocket upgrades in this alpha.

## Plan, Routes, And OpenAPI

The compiler records non-HTTP route kinds as `sse` or `websocket` in Plan route
metadata and marks the Plan with `runtime.realtime`. `sloppy routes` includes a
`kind` field, and `sloppy openapi` emits `x-slop-realtime` /
`x-slop-transport` on realtime operations.

## Current Limits

- SSE uses the bounded `Results.stream` descriptor path; it does not yet stream
  incrementally over a live socket with backpressure.
- WebSocket upgrade execution is unavailable and returns `501`.
- There is no browser client helper.
- Hubs are in-process bootstrap helpers only.
- No compression, replay buffer, external pub/sub, or cross-node fan-out.
