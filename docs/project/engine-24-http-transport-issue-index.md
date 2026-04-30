# ENGINE-24 HTTP Transport Issue Index

## Epic

- #411 EPIC ENGINE-24: HTTP Transport Runtime Server

## Implemented In This PR

- #412 TASK ENGINE-24.A: Transport Architecture and Libuv Boundary
- #413 TASK ENGINE-24.B: TCP Bind, Listen, and Accept Lifecycle
- #414 TASK ENGINE-24.C: Connection Read Loop and Request Accumulation

This PR line now covers the transport foundation through request-ready accumulation:
Slop-owned server/config/state, libuv isolation, bind/listen, accept, bounded connection
admission, overflow close, accepted-connection read loop, bounded TCP chunk accumulation,
Content-Length body accumulation through existing ENGINE-13 semantics, request-ready
parking, and cleanup.

## Follow-Ups

- #415 Dispatch and Response Write Loop
- #416 Transport Cancellation, Timeout, and Shutdown
- #417 Localhost Transport Smoke and Conformance
- #418 Keep-Alive Decision and Deferred HTTP/1.1 Upgrade Plan

## Non-Goals For #412/#413/#414

- no response write loop;
- no route dispatch from transport;
- no V8 handler execution;
- no SQLite/provider work;
- no TLS, HTTP/2, HTTP/3, WebSockets, keep-alive, HTTP pipelining, chunked/streaming
  bodies, static files, compression, reverse proxy behavior, benchmarks, or public alpha
  docs.
