# WebSocket Runtime Notes

WebSocket routes have two execution paths:

- app-host TestHost simulation for public API and lifecycle tests
- native HTTP/1.1 Upgrade execution in `sloppy run` for V8-backed routes

Do not treat one path as full coverage for the other. TestHost owns rich
app-level behavior such as auth helpers, message validation, heartbeat, idle
timeout, and bounded send queues. The native lane owns wire-level handshake,
frame parsing, connection upgrade, and V8 session delivery.

## Native Runtime Boundary

The libuv HTTP transport recognizes `Upgrade: websocket` before normal HTTP
dispatch. It validates the WebSocket handshake, writes `101 Switching
Protocols`, and moves the connection into WebSocket frame mode. In frame mode it
parses masked client frames, dispatches text and binary messages to the V8
session bridge, answers ping frames with pong frames, echoes close frames, and
uses the transport send helper for server frames.

`src/core/websocket.c` owns handshake validation and frame parser/writer logic.
`src/platform/libuv/http_transport_libuv.c` owns upgraded connection lifetime.
`src/engine/v8/websocket_bridge.cc` owns the JavaScript socket object and async
message iterator. `src/cli/cli_run.inc` connects those pieces to Plan route
matching and registered V8 handlers.

The V8 bridge still hides native transport handles. JavaScript receives an
engine-owned socket facade with `accept`, `messages`, `sendText`, `sendJson`,
`sendBytes`, and `close`; it never receives libuv streams or raw native
pointers.

## App-Host Simulation

`stdlib/sloppy/testing.js` owns the in-memory WebSocket test transport. It:

- matches WebSocket routes through the same route snapshot as HTTP TestHost
- runs route middleware and route auth before accept
- enforces origin and subprotocol policies before the handler accepts
- requires `socket.accept()` before sends
- represents inbound messages with an async iterator
- bounds inbound messages and outbound queued bytes
- records low-cardinality counters and redacted diagnostics
- closes active sockets when the host closes

The simulation is deterministic and test-runner neutral. It is not a protocol
parser and does not validate wire-level masking, opcodes, fragmentation, or
UTF-8 framing.

## Remaining Native Gaps

The native lane intentionally remains narrower than TestHost:

- no fragmented message reassembly
- no per-message compression
- no heartbeat or idle-timeout timers
- no native bounded send queue or app-host slow-client policy
- no native `message.json()` or schema validation helper on inbound messages
- no auth principal materialization for protected upgraded routes

Protected native WebSocket routes fail closed until the auth bridge can attach
the authenticated principal to the upgraded connection. Keep auth behavior
covered in TestHost until that native bridge exists.
