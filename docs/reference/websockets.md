# WebSocket Reference

This page is the compact lookup for the current WebSocket primitive contract.

## Supported Lane

| Lane | Status |
| --- | --- |
| `TestHost.create(app)` | Supports in-memory WebSocket connections. |
| `TestHost.fromArtifacts(...)` | Returns `501 SLOPPY_E_TESTHOST_WEBSOCKET_UNSUPPORTED`. |
| `TestHost.fromPackage(...)` | Returns `501 SLOPPY_E_TESTHOST_WEBSOCKET_UNSUPPORTED`. |
| Loopback TestHost | Returns `501 SLOPPY_E_TESTHOST_WEBSOCKET_UNSUPPORTED` unless a supplied runtime host implements `websocketConnect`. |
| Native `sloppy run` HTTP Upgrade | Unavailable. WebSocket route handlers return `501 SLOPPY_E_REALTIME_WEBSOCKET_UNAVAILABLE` through normal HTTP dispatch. |

## Route Metadata

WebSocket routes are `GET` routes with `kind: "websocket"`. Route snapshots
include:

- `metadata.realtime.kind: "websocket"`
- `metadata.realtime.websocket.protocols`
- `metadata.realtime.websocket.origins`
- `metadata.realtime.websocket.maxMessageBytes`
- `metadata.realtime.websocket.maxSendQueueBytes`
- `metadata.realtime.websocket.heartbeatMs`
- `metadata.realtime.websocket.idleTimeoutMs`
- `metadata.realtime.websocket.closeTimeoutMs`
- `metadata.realtime.websocket.compression`
- `metadata.realtime.websocket.slowClientPolicy`
- normal route `name`, `auth`, `tags`, and group metadata

Compiler Plan output records route `kind: "websocket"` and auth scope metadata
when the source uses static route builders.

## Handshake Checks In App-Host Tests

TestHost app-host WebSocket connections perform route-level checks before the
handler can accept:

- route path must match a WebSocket route
- `Origin` must match route `origins` when an origin policy is configured
- requested subprotocol must match `protocols` when protocols are configured
- route auth must succeed
- route scope checks must succeed

Rejected attempts throw from `connect().expectRejected(status)` with a
deterministic status.

## Close And Limit Behavior

| Condition | Behavior |
| --- | --- |
| Send before `accept()` | Throws `SLOPPY_E_WEBSOCKET_NOT_ACCEPTED`. |
| Send after close | Throws `SLOPPY_E_WEBSOCKET_CLOSED`. |
| Inbound message exceeds `maxMessageBytes` | Closes with `1009` and throws `SLOPPY_E_WEBSOCKET_MESSAGE_TOO_LARGE`. |
| Outbound message exceeds `maxMessageBytes` | Closes with `1009` and throws `SLOPPY_E_WEBSOCKET_MESSAGE_TOO_LARGE`. |
| Outbound queue exceeds `maxSendQueueBytes`, policy `"error"` | Throws `SLOPPY_E_WEBSOCKET_BACKPRESSURE`. |
| Outbound queue exceeds `maxSendQueueBytes`, policy `"close"` | Closes with `1013`. |
| Handler throws after accept | Closes with `1011` and records `SLOPPY_E_WEBSOCKET_HANDLER_ERROR`. |
| Idle timeout expires | Closes with `1001`. |
| Host closes active sockets | Closes with `1001`. |

## Message Kinds

| Kind | Fields | Helpers |
| --- | --- | --- |
| `text` | `text` | `json()`, `validate(schema)` when the text is JSON |
| `json` | `text` | `json()`, `validate(schema)` |
| `binary` | `bytes` | none |
| `ping` | `text` | none |
| `pong` | `text` | none |
| `close` | `code`, `reason` | none |

## Unsupported Protocol Features

The native runtime lane does not yet implement:

- `Sec-WebSocket-Accept` handshake response
- native frame parser/writer
- client masking validation
- fragmented frame handling
- native ping/pong timers
- native backpressure over libuv writes
- a V8 object that owns a long-lived upgraded transport connection

Those remain runtime work, not app-host behavior.
