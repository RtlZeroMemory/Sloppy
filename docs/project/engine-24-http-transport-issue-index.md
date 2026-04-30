# ENGINE-24 HTTP Transport Issue Index

## Epic

- #411 EPIC ENGINE-24: HTTP Transport Runtime Server

## Implemented In This PR

- #412 TASK ENGINE-24.A: Transport Architecture and Libuv Boundary
- #413 TASK ENGINE-24.B: TCP Bind, Listen, and Accept Lifecycle
- #414 TASK ENGINE-24.C: Connection Read Loop and Request Accumulation
- #415 TASK ENGINE-24.D: Dispatch and Response Write Loop
- #416 TASK ENGINE-24.E: Transport Cancellation, Timeout, and Shutdown
- #417 TASK ENGINE-24.F: Localhost Transport Smoke and Conformance

This PR line now covers the transport foundation through localhost smoke/conformance:
Slop-owned server/config/state, libuv isolation, bind/listen, accept, bounded connection
admission, overflow close, accepted-connection read loop, bounded TCP chunk accumulation,
Content-Length body accumulation through existing ENGINE-13 semantics, request-ready
parking, request-ready to dispatch transition, response serialization, TCP write, close
after response, client-disconnect cancellation, header/body/request/write timeout hooks,
deterministic timeout response when safe, shutdown rejection, immediate-cancel/drain-lite
active connection close, cleanup-once terminal callback behavior, and default non-V8 real
localhost TCP smoke for the implemented MVP request/response and failure policy.

## Follow-Ups

- #418 Keep-Alive Decision and Deferred HTTP/1.1 Upgrade Plan

## Non-Goals For #412/#413/#414/#415/#416/#417

- no V8 handler execution;
- no users API proof;
- no SQLite/provider work;
- no TLS, HTTP/2, HTTP/3, WebSockets, keep-alive, HTTP pipelining, chunked/streaming
  bodies, static files, compression, reverse proxy behavior, benchmarks, or public alpha
  docs.
