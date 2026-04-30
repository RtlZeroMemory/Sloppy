# HTTP Transport Current State Audit

## Status

ENGINE-24.A/B/C is now implemented through request-ready accumulation. Sloppy has a
Slop-owned HTTP transport server boundary that can initialize, bind, listen, accept TCP
connections, start a bounded read loop, accumulate one Content-Length request from TCP
chunks, parse it through existing ENGINE-13 HTTP semantics, stop, and dispose without
exposing libuv handles through public runtime or JavaScript APIs.

Before this slice, HTTP socket serving lived in the dev-only CLI path and the core
`SlHttpBackend` modeled lifecycle/admission without owning a real platform listener. The
new transport layer keeps that existing backend model and adds the first reusable
libuv-backed TCP listener under `src/platform/libuv/`.

## Implemented

- `include/sloppy/http_transport.h` defines server config/state, connection placeholders,
  lifecycle APIs, and query helpers without libuv types.
- `src/platform/libuv/http_transport_libuv.c` owns `uv_loop_t`, `uv_tcp_t` listener/client
  handles, bind/listen/accept callbacks, overflow close behavior, and cleanup.
- Accepted connections claim bounded connection slots and start the read loop.
- TCP chunks append into fixed per-connection accumulation storage through `SlByteBuilder`.
- Request heads are detected at `CRLFCRLF` and bounded by configured head bytes.
- Content-Length bodies are accumulated through existing ENGINE-13 body-reader policy.
- A complete request transitions to request-ready and is parked for the later dispatch/write
  loop.
- Connection overflow is deterministic: the pending socket is accepted only to close it,
  counters record rejection, and no unbounded queue is created.
- Stop/dispose close listener and accepted/read/request-ready connection handles exactly
  once.

## Still Deferred

- #415 owns dispatch from transport and response write loop.
- #416 owns transport cancellation, timeout, and shutdown completion.
- #417 owns localhost transport smoke/conformance beyond the bounded unit smoke.
- #418 owns keep-alive decision and any HTTP/1.1 upgrade plan.

There is no keep-alive, TLS, HTTP/2, HTTP/3, WebSockets, streaming, compression, static
file serving, reverse proxy behavior, benchmark evidence, production-edge claim, or public
alpha documentation from this slice.
