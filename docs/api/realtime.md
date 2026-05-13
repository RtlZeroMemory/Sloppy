# Realtime

`Realtime` is Sloppy's high-level framework for typed WebSocket application
events. Use it when an app needs named client/server events, schema validation,
groups, single-process presence, per-message authorization, and TestHost
coverage.

Raw WebSocket routes remain available through `app.websocket(...)`. Realtime
routes build on those primitives and are the recommended application API for
bidirectional features.

```ts
import { Realtime, Sloppy, schema } from "sloppy";

const Chat = Realtime.channel("chat", {
    client: {
        sendMessage: schema.object({
            roomId: schema.string(),
            text: schema.string().maxLength(1000),
        }),
        typing: Realtime.event(schema.object({
            roomId: schema.string(),
        })).requiresScope("chat:write"),
    },
    server: {
        messageCreated: schema.object({
            id: schema.string(),
            roomId: schema.string(),
            text: schema.string(),
            createdAt: schema.string(),
        }),
        userTyping: schema.object({
            roomId: schema.string(),
            userId: schema.string(),
        }),
    },
});

const app = Sloppy.create();

app.realtime("/rooms/{roomId}", Chat, async (ctx) => {
    await ctx.accept();
    await ctx.groups.join(`room:${ctx.params.roomId}`);

    ctx.on("sendMessage", async (input) => {
        await ctx.group(`room:${ctx.params.roomId}`).broadcast("messageCreated", {
            id: crypto.randomUUID(),
            roomId: ctx.params.roomId,
            text: input.text,
            createdAt: new Date().toISOString(),
        });
    });

    ctx.on("typing", async (input) => {
        await ctx.group(`room:${input.roomId}`).broadcast("userTyping", {
            roomId: input.roomId,
            userId: ctx.user.sub,
        }, { exceptSelf: true });
    });
})
    .requiresAuth()
    .requiresScope("chat")
    .allowedOrigins(["https://app.example.com"]);
```

## Channels

`Realtime.channel(name, definition)` creates an immutable channel descriptor.
Channel names and event names must be stable identifiers. Reserved protocol
event names such as `connect`, `disconnect`, `error`, `ping`, `pong`, `join`,
`leave`, and `system` are rejected.

Client events describe messages the browser or test client can send. Server
events describe messages the app can emit. Schemas must be Sloppy schemas, and
the same event name cannot appear in both maps.

`Realtime.event(schema)` wraps a schema when the event also needs authorization
metadata:

```ts
const Update = Realtime.event(schema.object({ id: schema.string() }))
    .requiresAuth()
    .requiresScope("items:write")
    .requiresRole("operator");
```

## Envelopes

Realtime messages are JSON envelopes:

```json
{ "type": "sendMessage", "data": { "text": "hello" }, "id": "optional-id" }
```

Server messages use the same shape. Realtime validates client envelopes before
calling a handler and validates server envelopes before sending. Unknown events,
malformed envelopes, invalid payloads, unauthorized events, handler failures,
and backplane failures produce bounded error envelopes or close the socket
according to the route policy.

## Route Options

`app.realtime(pattern, channel, handler, options?)` registers a WebSocket route
with realtime metadata. Route groups expose `group.realtime(...)`.

| Option | Behavior |
| --- | --- |
| `protocols` | WebSocket subprotocols. Defaults to the channel protocol. |
| `origins` | Allowed origins as strings or `"*"`. |
| `maxMessageBytes` | Inbound and outbound message limit. |
| `maxSendQueueBytes` | Per-socket outbound queue limit in TestHost. |
| `heartbeatMs` | App-host heartbeat ping interval. |
| `idleTimeoutMs` | App-host idle close timeout. |
| `closeTimeoutMs` | Close wait budget metadata. |
| `presence` | Enables the single-process presence API. |
| `backplane` | Realtime backplane object. Defaults to memory. |
| `unknownEventPolicy` | `"error"` or `"close"`. Defaults to `"error"`. |
| `validationFailurePolicy` | `"error"` or `"close"`. Defaults to `"error"`. |
| `handlerErrorPolicy` | `"error"` or `"close"`. Defaults to `"close"`. |

## Context

Realtime handlers receive `ctx`:

| Member | Behavior |
| --- | --- |
| `ctx.accept()` | Accepts the WebSocket and registers the connection. |
| `ctx.on(event, handler)` | Handles a client event. Duplicate handlers are rejected. |
| `ctx.on(event, policy, handler)` | Adds per-message scope or role checks. |
| `ctx.send(event, data)` | Sends a validated server event to this connection. |
| `ctx.broadcast(event, data)` | Broadcasts to all connections on this realtime route. |
| `ctx.group(name)` | Returns a group sender. |
| `ctx.groups.join(name)` | Adds this connection to a group. |
| `ctx.groups.leave(name)` | Removes this connection from a group. |
| `ctx.groups.list()` | Lists groups for this connection. |
| `ctx.presence` | Single-process presence helpers when enabled. |
| `ctx.params`, `ctx.query`, `ctx.headers` | Handshake request data. |
| `ctx.user`, `ctx.requireUser()` | Auth principal from the route. |
| `ctx.services` | Request service scope. |
| `ctx.connectionId` | Connection identifier. |

Group names are bounded strings without control characters. Leaving an unknown
group is safe. Broadcast to an empty group succeeds with count `0`.

## Presence

Presence is single-process and opt-in:

```ts
await ctx.presence.set({
    metadata: { status: "online", displayName: "Alice" },
});

const users = await ctx.presence.inGroup(`room:${ctx.params.roomId}`);
```

Presence records contain `connectionId`, optional `userId`, current groups,
`connectedAt`, and bounded JSON metadata. Sloppy does not store tokens or raw
credentials in presence records.

## Backplane

`Realtime.backplane.memory()` returns the in-process backplane used by default.
It owns connection tracking, group membership, group broadcast, direct sends,
presence records, disposal, and a small health snapshot.

Other backplanes can implement the same method shape in separate packages. The
memory backplane is not a distributed broker and does not guarantee
multi-process group broadcast or distributed presence.

## TestHost

`TestHost.create(app)` exposes high-level realtime helpers:

```ts
await using host = await TestHost.create(app);

const alice = await host.realtime("/rooms/r1", Chat)
    .asUser({ sub: "alice", scopes: ["chat", "chat:write"] })
    .origin("https://app.example.com")
    .connect();

await alice.send("sendMessage", { roomId: "r1", text: "hello" });
await alice.expect("messageCreated", { roomId: "r1", text: "hello" });
await alice.expectError("SLOPPY_E_REALTIME_VALIDATION_FAILED");
await alice.close();
```

The TestHost client validates outgoing client events and validates incoming
server events against the channel. `expect(...)` accepts a partial expected data
object so tests can ignore generated fields such as IDs and timestamps.

Artifact, package, and loopback TestHost WebSocket helpers keep the same
support boundary as raw WebSockets: they report
`SLOPPY_E_TESTHOST_WEBSOCKET_UNSUPPORTED` unless the supplied runtime host
provides a WebSocket connector.

## Metrics

Realtime emits low-cardinality metrics in app-host tests:

- `realtime.connections.total`
- `realtime.connections.active` as a gauge
- `realtime.messages.in.total`
- `realtime.messages.out.total`
- `realtime.messages.validation_failed.total`
- `realtime.messages.unauthorized.total`
- `realtime.groups.join.total`
- `realtime.groups.leave.total`
- `realtime.groups.broadcast.total`
- `realtime.presence.set.total`
- `realtime.errors.total`
- `realtime.backplane.errors.total`

Labels use route pattern, channel name, event name, outcome, and error code.
They do not include raw paths, user IDs, group names, message payloads, tokens,
or cookies.

`Health.realtime(backplane)` reports the configured backplane health through
the same health-check shape as the rest of `Health`.

## Plan Metadata

Compiler and CLI metadata for `app.realtime(...)` is intentionally partial in
this alpha. Plan, `sloppy routes`, and OpenAPI preserve the transport plus the
static channel/options expression text and mark realtime metadata with
`metadataStatus: "partial"`. They do not claim complete event-name, per-event
auth, or schema extraction yet.

## Runtime Boundary

The high-level framework has complete app-host/TestHost coverage and generated
`runtime-classic` support for public V8-backed routes. Native `sloppy run` uses
the raw WebSocket backend documented in [`WebSockets`](./websockets.md).
Protected native WebSocket routes still fail closed until auth principals are
materialized on upgraded connections.

Do not describe the memory backplane as Redis, pub/sub, distributed presence,
or cross-process fan-out.
