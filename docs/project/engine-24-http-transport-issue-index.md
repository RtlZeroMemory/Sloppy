# ENGINE-24 HTTP Transport Issue Index

## Epic

- #411 EPIC ENGINE-24: HTTP Transport Runtime Server

## Implemented In This PR

- #412 TASK ENGINE-24.A: Transport Architecture and Libuv Boundary
- #413 TASK ENGINE-24.B: TCP Bind, Listen, and Accept Lifecycle

This PR implements the transport listener foundation only: Slop-owned server/config/state,
libuv isolation, bind/listen, accept, bounded connection admission, overflow close, and
cleanup.

## Follow-Ups

- #414 Connection Read Loop and Request Accumulation
- #415 Dispatch and Response Write Loop
- #416 Transport Cancellation, Timeout, and Shutdown
- #417 Localhost Transport Smoke and Conformance
- #418 Keep-Alive Decision and Deferred HTTP/1.1 Upgrade Plan

## Non-Goals For #412/#413

- no request read loop;
- no request accumulation;
- no HTTP parser integration from TCP chunks;
- no response write loop;
- no route dispatch from transport;
- no V8 handler execution;
- no SQLite/provider work;
- no TLS, HTTP/2, HTTP/3, WebSockets, keep-alive, pipelining, streaming, static files,
  compression, reverse proxy behavior, benchmarks, or public alpha docs.
