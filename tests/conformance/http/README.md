# HTTP Conformance

Status: ENGINE-19.BC executable lane registration.

This lane covers HTTP parser, route table, synthetic dispatch, body policy, response
mapping, and localhost transport behavior that is already implemented. It does not add or
claim production-edge HTTP behavior.

## Default Non-V8 Cases

`conformance.http.default_dispatch` runs the existing `core.http.dispatch` executable under
an ENGINE-19 name. It proves the documented synthetic dispatch lane:

- GET/POST/PUT/PATCH/DELETE metadata can reach the engine boundary;
- route miss returns the documented route-not-found diagnostic;
- method mismatch returns method-not-allowed before handler execution;
- route parameters can match through dispatch;
- query parsing rejects malformed query bytes deterministically;
- JSON/text body policy rejects malformed JSON, unsupported media, unsupported body
  framing, and oversized bodies before handler execution;
- missing plan handler fails before engine entry.

This is default non-V8 evidence. It uses the noop engine where handler execution is out of
scope, so it does not prove V8 handler execution.

## Localhost Transport Cases

`conformance.transport.localhost_mvp` runs the existing `core.http.transport` executable
under an ENGINE-19 name. It proves the localhost transport lane:

- raw TCP bytes over `127.0.0.1` with an ephemeral port;
- GET success, POST text body success, route miss, method mismatch, and safe dispatch
  failure mapping;
- malformed request, body-too-large, unsupported media, unsupported
  `Transfer-Encoding`, and unsupported pipelined bytes;
- response `Content-Length`, `Connection: close`, one request per connection, and
  close-after-response cleanup;
- client disconnect, timeout, shutdown, response-capacity failure, and cleanup-once paths.

This is loopback MVP evidence only. It is not TLS, HTTP/2, HTTP/3, WebSockets, keep-alive,
pipelining, streaming, reverse-proxy, production-edge, benchmark, or V8 evidence.

## V8-Gated HTTP Cases

`conformance.v8.http_dispatch_execution` is registered only when `SLOPPY_ENABLE_V8=ON`.
It runs the existing HTTP dispatch integration executable and proves synthetic HTTP request
dispatch through the V8 handler boundary, including non-GET methods, request headers, JSON
body materialization, missing function diagnostics, and thrown handler diagnostics.

When the V8 SDK is missing, this lane is skipped/not configured, not passed.
