# HTTP Module

## Purpose

The HTTP module owns Sloppy's native HTTP parser, route matching, dispatch, response
serialization, backend lifecycle, and localhost transport foundations.

## Current Status

Implemented behavior includes:

- bounded complete-buffer request-head parsing;
- route pattern parsing and matching for the supported path subset;
- Plan-backed route table construction and handler dispatch;
- request context and result conversion in V8-gated lanes;
- response serialization for the supported native response model;
- backend admission, lifecycle, cancellation, timeout, and diagnostic state;
- libuv-backed localhost transport with bounded connection storage;
- sequential HTTP/1.1 keep-alive and scoped chunked behavior in the supported native/runtime
  lanes.

## Non-Claims

This is not a production HTTP edge server. Sloppy does not currently claim production
graceful drain, HTTP/2, HTTP/3, WebSockets, SSE, static file serving, public streaming
response APIs, TLS, or broad middleware/framework behavior.

## Invariants

- Platform socket/libuv details stay behind platform boundaries.
- Request bytes and parsed views have explicit arena/request lifetimes.
- Pipelined bytes are rejected by the current contract.
- Unsupported media, malformed heads, body limits, timeout, disconnect, and shutdown paths
  produce deterministic diagnostics where covered.
- HTTP evidence must distinguish parser/backend/transport/default lanes from V8, TLS,
  production-edge, stress/torture, and benchmark lanes.

## Related Docs

- `docs/project/http-transport-runtime-architecture.md`
- `docs/project/http-client-api-architecture.md`
- `docs/testing-strategy.md`
