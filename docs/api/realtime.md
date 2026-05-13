# Realtime

`Realtime` is the experimental route surface for server-sent events,
app-host WebSocket primitives, and in-process hub helpers.

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

`app.websocket(pattern, handler, options?)` and
`group.websocket(pattern, handler, options?)` register `GET` routes with
realtime kind `websocket`. `app.ws(...)` and `group.ws(...)` remain aliases.

```ts
const ClientMessage = schema.object({
    type: schema.enum(["ping", "echo"]),
    text: schema.string().optional(),
});

app.websocket("/ws", async (socket) => {
    await socket.accept();

    for await (const message of socket.messages()) {
        if (message.kind === "text") {
            await socket.sendText(`echo:${message.text}`);
            continue;
        }

        if (message.kind === "json") {
            const input = message.validate(ClientMessage);
            if (input.type === "ping") {
                await socket.sendJson({ type: "pong" });
            }
        }
    }
}, {
    origins: ["https://app.example.com"],
    protocols: ["sloppy.realtime"],
    maxMessageBytes: 64 * 1024,
    maxSendQueueBytes: 1024 * 1024,
    heartbeatMs: 15_000,
    idleTimeoutMs: 30_000,
});
```

Options are validated at route registration. `protocols` must be WebSocket
subprotocol tokens. `origins` must be explicit strings or `"*"`. Message and
queue limits must be positive integers. Compression is rejected unless it is
`false`.

Route builders support WebSocket-specific metadata:

```ts
app.websocket("/secure/ws", async (socket) => {
    const user = socket.ctx.user;
    await socket.accept();
    await socket.sendJson({ type: "hello", sub: user.sub });
})
    .withName("Realtime.Secure")
    .requiresAuth()
    .requiresScope("realtime")
    .allowedOrigins(["https://app.example.com"]);
```

## `Realtime.sse(handler, options?)`

Wraps a handler in a `Results.stream(...)` descriptor with:

- `Content-Type: text/event-stream`
- `Cache-Control: no-cache`
- `X-Slop-Realtime: sse`

`options.maxQueuedEvents` bounds buffered SSE frame writes for one handler
invocation. The default is `64`; writing more frames rejects deterministically
instead of growing memory without limit. The handler writes bounded chunks into a
`Results.stream` descriptor before returning. Native HTTP/1.1 serialization
lowers that descriptor into the Core stream path and emits chunked frames, but
this is still bounded descriptor streaming, not a production push transport with
live handler backpressure.

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

## WebSocket Socket

The handler receives a socket object. New handlers should use
`async (socket) => { ... }`; legacy two-argument handlers receive
`(ctx, socket)`.

| Member | Behavior |
| --- | --- |
| `socket.ctx` | Request context, including auth, services, config, metrics, route, and request metadata. |
| `socket.accept()` | Accepts the app-host WebSocket connection. Sends before accept fail. |
| `socket.messages()` | Async iterator of inbound messages. |
| `socket.sendText(text)` | Sends a text message. |
| `socket.sendJson(value)` | Sends a JSON message. |
| `socket.sendBytes(bytes)` | Sends a binary message in app-host tests. |
| `socket.close(code?, reason?)` | Closes idempotently. |
| `socket.closed` | `true` after close. |
| `socket.protocol` | Selected subprotocol, or an empty string. |
| `socket.id` | Test-host connection ID in app-host tests. |

Messages have `kind`, plus kind-specific data. Text messages expose `text`;
binary messages expose `bytes`; JSON messages can be read through `json()` or
validated with `validate(schema)`.

`Realtime.websocket(handler, options?)` wraps a handler with the same route
handler marker used by `app.websocket(...)`. Direct HTTP calls to a WebSocket
route still return the unavailable response; use TestHost WebSocket helpers to
exercise the socket path.

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
`x-slop-transport` on realtime operations. Standard OpenAPI does not model
WebSocket message flow; Sloppy does not present WebSocket routes as ordinary
HTTP response operations.

## Current Limits

- SSE uses the bounded `Results.stream` descriptor path and native Core stream
  serialization; the handler does not stay attached to a live socket after it
  returns.
- WebSocket app-host execution is available through `TestHost.create(app)`.
- Native HTTP upgrade execution is unavailable and returns `501`.
- Artifact/package TestHost WebSocket connections report
  `SLOPPY_E_TESTHOST_WEBSOCKET_UNSUPPORTED` until a runtime lane supports real
  upgrade execution.
- There is no browser client helper.
- Hubs are in-process bootstrap helpers only.
- No compression, replay buffer, external pub/sub, or cross-node fan-out.
