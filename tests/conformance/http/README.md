# HTTP Conformance

This directory records executable lane registration.
This lane covers HTTP parser, route table, synthetic dispatch, body policy,
response mapping, and localhost transport behavior that is already
implemented. Production-edge HTTP behavior is separate work.

## Default Non-V8 Cases

`conformance.http.default_dispatch` runs the existing `core.http.dispatch` executable under
the conformance HTTP lane. It validates the documented synthetic dispatch lane:

- GET/POST/PUT/PATCH/DELETE metadata can reach the engine boundary;
- route miss returns the documented route-not-found diagnostic;
- method mismatch returns method-not-allowed before handler execution;
- route parameters can match through dispatch;
- query parsing rejects malformed query bytes deterministically;
- JSON/text body policy rejects malformed JSON, unsupported media, unsupported body
  framing, and oversized bodies before handler execution;
- missing plan handler fails before engine entry.

This is default non-V8 evidence. It uses the noop engine where handler execution is out of
scope, so it does not validate V8 handler execution.

## Localhost Transport Cases

`core.http.transport` remains the detailed executable for the complete localhost transport
suite. The conformance registrations run targeted cases from that executable so the
default CI lane keeps the evidence names visible without rerunning the full transport
suite under every alias. Together, those cases validate the localhost transport lane:

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
- HTTPS loopback with runtime-generated local certificate/key material, OpenSSL-backed
  handshake, handler dispatch, and `https` connection scheme propagation;
- TLS failure evidence for missing/invalid certificate and key paths, encrypted private
  key passphrase missing/wrong/correct handling, plaintext handshake failure cleanup before
  dispatch, and shutdown cleanup with an active TLS connection;
- client disconnect, timeout, shutdown, response-capacity failure, and cleanup-once paths.

The HTTP lane also registers matrix-aligned targeted aliases over the same bounded executable:

- `conformance.transport.keep_alive`;
- `conformance.transport.keep_alive_idle_timeout`;
- `conformance.transport.keep_alive_max_requests`;
- `conformance.transport.lifecycle_reset`;
- `conformance.transport.chunked_request`;
- `conformance.transport.streaming_response`;
- `conformance.transport.backpressure`;
- `conformance.transport.https_loopback`;
- `conformance.transport.https_tls_negative`;
- `conformance.transport.shutdown_cancel`;
- `smoke.transport.keep_alive_streaming_bounded`.

The smoke alias exercises bounded repeated keep-alive requests on one connection, repeated
short-lived keep-alive connections, repeated chunked requests, repeated streaming
responses, repeated malformed requests, and shutdown/cleanup counters. It is stress/smoke
metadata coverage: no throughput, latency, scalability, external-runtime comparison, or
performance comparison.

This is bounded loopback transport metadata coverage. The HTTPS case validates the current
OpenSSL server wrapper on localhost, not production TLS hardening. This is not HTTP/2,
HTTP/3, ALPN, mTLS, WebSockets, SSE, multipart/file upload, compression, static files,
reverse-proxy, production-edge, benchmark, public streaming API, or V8 evidence.

## V8-Gated HTTP Cases

`conformance.v8.http_dispatch_execution` is registered only when `SLOPPY_ENABLE_V8=ON`.
It runs the existing HTTP dispatch integration executable and validates synthetic HTTP request
dispatch through the V8 handler boundary, including non-GET methods, request headers, JSON
body materialization, missing function diagnostics, and thrown handler diagnostics.

When the V8 SDK is missing, this lane is skipped/not configured, not passed.
