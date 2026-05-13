# WebSockets

WebSocket routes are app-host primitives for long-lived bidirectional tests and
early API design. They use normal Sloppy route matching, middleware, auth, route
metadata, and TestHost diagnostics.

```ts
import { Sloppy, schema } from "sloppy";

const app = Sloppy.create();

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
            } else {
                await socket.sendJson({ type: "echo", text: input.text ?? "" });
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

## Route Registration

`app.websocket(pattern, handler, options?)` and
`group.websocket(pattern, handler, options?)` register `GET` routes with
`kind: "websocket"`. `app.ws(...)` and `group.ws(...)` are aliases.

The route builder supports the ordinary route metadata chain plus WebSocket
origin metadata:

```ts
app.websocket("/secure/ws", async (socket) => {
    const user = socket.ctx.requireUser();
    await socket.accept();
    await socket.sendJson({ type: "hello", sub: user.sub });
})
    .withName("Realtime.Secure")
    .requiresAuth()
    .requiresScope("realtime")
    .allowedOrigins(["https://app.example.com"]);
```

## Options

| Option | Behavior |
| --- | --- |
| `protocols` | Allowed WebSocket subprotocol tokens. When configured, the client must request one of them. |
| `origins` | Allowed browser origins as strings, or `"*"`. Missing `Origin` is allowed for non-browser tests. |
| `maxMessageBytes` | Maximum inbound or outbound message payload size. Defaults to `64 * 1024`. |
| `maxSendQueueBytes` | Maximum queued outbound bytes per socket. Defaults to `1024 * 1024`. |
| `heartbeatMs` | App-host TestHost heartbeat ping interval. Omit to disable. |
| `idleTimeoutMs` | App-host TestHost idle close timeout. Omit to disable. |
| `closeTimeoutMs` | Close wait budget metadata. Defaults to `5000`. |
| `compression` | Must be `false` when provided. Compression is not supported. |
| `slowClientPolicy` | `"error"` rejects the send; `"close"` closes with `1013`. Defaults to `"error"`. |

## Socket API

| Member | Behavior |
| --- | --- |
| `socket.ctx` | Request context with auth, services, config, route, request, and connection metadata. |
| `socket.accept()` | Accepts the app-host WebSocket. Sends before accept fail. |
| `socket.close(code?, reason?)` | Closes idempotently. |
| `socket.sendText(text)` | Sends a text message. |
| `socket.sendJson(value)` | Sends a JSON message. |
| `socket.sendBytes(bytes)` | Sends a binary message. |
| `socket.sendPing(payload?)` | Sends a ping message in app-host tests. |
| `socket.sendPong(payload?)` | Sends a pong message in app-host tests. |
| `socket.messages()` | Async iterator for inbound messages. |
| `socket.closed` | `true` after close. |
| `socket.protocol` | Selected subprotocol, or an empty string. |
| `socket.id` | App-host TestHost connection ID. |
| `socket.remoteAddress` | App-host remote address label. |
| `socket.request` | Request object from the handshake context. |

Messages expose `kind`. Text messages have `text`; binary messages have
`bytes`; JSON messages can be read with `json()` or validated with
`validate(schema)`.

## TestHost

`TestHost.create(app)` supports in-memory WebSocket connections:

```ts
await using host = await TestHost.create(app);

const ws = await host.websocket("/ws")
    .origin("https://app.example.com")
    .protocols(["sloppy.realtime"])
    .connect();

await ws.sendText("hello");
await ws.expectText("echo:hello");
await ws.sendJson({ type: "ping" });
await ws.expectJson({ type: "pong" });
await ws.close();
```

Auth helpers mirror HTTP request builders:

```ts
await host.websocket("/secure/ws").connect().expectRejected(401);

const ws = await host.websocket("/secure/ws")
    .withJwt({ sub: "u1", scope: "realtime" }, { secret: "test-secret" })
    .connect();
```

Artifact, package, and loopback hosts report
`SLOPPY_E_TESTHOST_WEBSOCKET_UNSUPPORTED` with status `501` until a runtime
lane supports real HTTP Upgrade and frame I/O.

## Metrics And Diagnostics

App-host TestHost records low-cardinality WebSocket counters by route pattern,
message kind, outcome, and close code:

- `websocket.upgrades.total`
- `websocket.upgrades.rejected.total`
- `websocket.connections.total`
- `websocket.messages.in.total`
- `websocket.messages.out.total`
- `websocket.bytes.in.total`
- `websocket.bytes.out.total`
- `websocket.close.total`
- `websocket.backpressure.total`

Diagnostics use route patterns and failure codes. They do not include message
bodies, raw tokens, or user IDs.

## Runtime Support

The app-host/TestHost lane supports the socket API above. Native `sloppy run`
does not yet expose a long-lived upgraded socket to V8. Direct HTTP execution
of a WebSocket route returns `501 SLOPPY_E_REALTIME_WEBSOCKET_UNAVAILABLE`, and
TestHost runtime modes fail with the explicit unsupported diagnostic.
