# HTTP Conformance

Status: ENGINE-19.BC executable lane registration plus HTTP-25.F evidence registration.

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

`core.http.transport` remains the detailed executable for the complete localhost transport
suite. The conformance registrations run targeted cases from that executable so the
default CI lane keeps the evidence names visible without rerunning the full transport
suite under every alias. Together, those cases prove the localhost transport lane:

- raw TCP bytes over `127.0.0.1` with an ephemeral port;
- GET success, POST text body success, route miss, method mismatch, and safe dispatch
  failure mapping;
- sequential HTTP/1.1 keep-alive reuse for two and N requests on one connection;
- POST followed by GET on the same connection with request-owned method, path, body, and
  response state reset between requests;
- `Connection: close`, disabled keep-alive config, HTTP/1.0 close policy, max requests per
  connection, idle timeout, shutdown closing idle keep-alive connections, and shutdown
  during active write cleanup;
- malformed request, body-too-large, unsupported media, unsupported transfer encoding,
  unsupported pipelined bytes, bounded chunked request decoding, chunked split-read and
  multi-chunk success, empty chunked bodies, invalid chunk sizes, malformed delimiters,
  decoded-body overflow, conflicting `Content-Length`/`Transfer-Encoding`, and rejected
  trailers;
- internal/native streaming response chunk framing, multiple chunks, empty streaming
  response, final zero-size chunk, omitted `Content-Length` with `Transfer-Encoding:
  chunked`, keep-alive after streaming completion, streaming backpressure rejection, and
  no-write-after-terminal cleanup paths;
- client disconnect, timeout, shutdown, response-capacity failure, and cleanup-once paths.

HTTP-25.F also registers matrix-aligned targeted aliases over the same bounded executable:

- `conformance.transport.keep_alive`;
- `conformance.transport.keep_alive_idle_timeout`;
- `conformance.transport.keep_alive_max_requests`;
- `conformance.transport.lifecycle_reset`;
- `conformance.transport.chunked_request`;
- `conformance.transport.streaming_response`;
- `conformance.transport.backpressure`;
- `conformance.transport.shutdown_cancel`;
- `smoke.transport.keep_alive_streaming_bounded`.

The smoke alias exercises bounded repeated keep-alive requests on one connection, repeated
short-lived keep-alive connections, repeated chunked requests, repeated streaming
responses, repeated malformed requests, and shutdown/cleanup counters. It is stress/smoke
evidence only: no throughput, latency, scalability, external-runtime comparison, or
performance claim.

This is bounded loopback transport evidence only. It is not TLS, HTTP/2, HTTP/3,
WebSockets, SSE, multipart/file upload, compression, static files, reverse-proxy,
production-edge, benchmark, public streaming API, or V8 evidence.

## V8-Gated HTTP Cases

`conformance.v8.http_dispatch_execution` is registered only when `SLOPPY_ENABLE_V8=ON`.
It runs the existing HTTP dispatch integration executable and proves synthetic HTTP request
dispatch through the V8 handler boundary, including non-GET methods, request headers, JSON
body materialization, missing function diagnostics, and thrown handler diagnostics.

When the V8 SDK is missing, this lane is skipped/not configured, not passed.
