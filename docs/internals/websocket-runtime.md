# WebSocket Runtime Notes

WebSocket routes currently have two separate execution paths:

- app-host TestHost simulation for public API and lifecycle tests
- native HTTP dispatch fallback that returns an unavailable WebSocket response

Do not treat the app-host simulation as native transport coverage.

## Current Native Boundary

The libuv HTTP transport parses a bounded HTTP request, materializes a
`SlHttpRequestLifecycle`, dispatches the route, and writes a bounded
`SlHttpResponse`. It has a dedicated h2c Upgrade continuation, but there is no
equivalent WebSocket continuation that keeps the socket in an upgraded state.

The V8 HTTP bridge materializes request context objects and converts handler
return values into `Results.*` descriptors. It does not expose raw transport
handles, libuv streams, frame readers, or long-lived socket resources to
JavaScript.

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

## Native Work Required For Real Upgrade Support

A real runtime lane needs these pieces in order:

1. HTTP/1.1 Upgrade recognition for `Upgrade: websocket`, `Connection:
   Upgrade`, `Sec-WebSocket-Key`, and `Sec-WebSocket-Version: 13`.
2. A response path that writes `101 Switching Protocols` with
   `Sec-WebSocket-Accept` and then transfers the connection out of normal HTTP
   response dispatch.
3. A bounded frame parser for masked client frames, control-frame validation,
   close parsing, ping/pong, text, binary, and size limits.
4. A bounded writer with queue accounting and deterministic slow-client policy.
5. A V8 resource object that can outlive the initial request dispatch while
   still respecting owner-thread, cleanup, cancellation, and service-scope
   rules.
6. Shutdown integration so server stop closes active sockets and disposes
   route scopes.

Until those pieces exist, native runtime modes must fail clearly instead of
pretending that app-host behavior is transport support.
