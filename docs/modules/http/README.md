# HTTP Module

## Purpose

The HTTP module owns Sloppy's inbound HTTP/1.1 parser, route matching, dispatch,
request-context materialization, response serialization, backend lifecycle, localhost
transport foundations, and bounded HTTPS/TLS wrapping. HTTP/1.1 is the first backend, not
the app-facing abstraction.

## Current Status

HTTP-SERVER-01 establishes the current internal alpha server contract. The default native
lane covers parser, backend, dispatch, response serialization, localhost transport,
keep-alive, chunked request decoding, streaming response descriptors, inbound OpenSSL TLS
wrapping, shutdown/cancel paths, and fuzz seed replay. V8-gated evidence covers handler
dispatch and request context materialization when the V8 lane is configured.

## HTTP/1.1 Alpha Contract

Implemented behavior includes:

- bounded complete-buffer request-head parsing with explicit request-line, target, header
  count, header-name, header-value, total-header-byte, and body limits;
- supported origin-form request targets, query parsing, and HTTP/1.0/HTTP/1.1 version
  validation;
- HTTP/1.1 Host policy requiring exactly one non-empty `Host` header;
- singleton framing policy that rejects duplicate `Content-Length`, duplicate
  `Transfer-Encoding`, and `Content-Length` plus `Transfer-Encoding` conflicts;
- bounded `Content-Length` bodies and bounded chunked request decoding, with unsupported
  transfer codings and trailers rejected;
- JSON, text, and octet-stream request body classification for the V8-gated handler lane;
- Plan-backed route table construction and handler dispatch;
- protocol-abstract request context fields for request metadata, route/query data,
  connection metadata, cancellation signal, and deadline status marker;
- response serialization for fixed native responses and scoped native/runtime streaming
  responses with validated headers and chunked framing;
- libuv-backed localhost transport with bounded connection storage, sequential HTTP/1.1
  keep-alive, idle timeout, max requests per connection, close policy, and deterministic
  pipelining rejection;
- optional inbound HTTPS wrapping through OpenSSL-owned TLS server state, strict certificate/key
  config and key-material loading validation, generated local-cert loopback evidence, native
  passphrase handling, and `https` request-context scheme propagation after handshake;
- backend admission, lifecycle, cancellation, timeout, disconnect, shutdown, and
  diagnostic state;
- `sloppy run` consumption of bounded server config metadata for host/port, max
  connections, max request body bytes, request timeout, keep-alive policy, and opt-in TLS
  certificate/key listener settings.

## Request Context

The current V8-gated context shape is intentionally protocol-abstract. It exposes
request, route, query, connection, signal, and deadline data without exposing parser,
socket, libuv, or native handle internals. Current request fields include method, scheme,
protocol, path, raw target, query string, headers, body helpers, content type, content
length, and a string request ID when a transport lifecycle provides one.

HTTP listeners set `scheme` to `http`. HTTPS listeners set `scheme` to `https` after the
TLS handshake completes and expose `ctx.connection.secure === true` without exposing TLS
or socket handles.

`ctx.signal` is the request cancellation token surface for handler code that needs to fail
before dispatching work after the request has already been cancelled. `ctx.deadline` is a
status marker for the current request lifecycle. It is `null` for the normal non-expired
request path and becomes an elapsed-deadline object only when the native request deadline
has already fired. Provider APIs may accept both values in operation options for
pre-dispatch cancellation/deadline checks, but that does not imply provider-specific
mid-query interruption unless the provider lane documents and tests that behavior.

`ctx.request.body.bytes()`, `text()`, and `json()` are consumed-once helpers for the
bounded buffered body. The existing top-level `ctx.request.bytes()`, `text()`, and
`json()` remain repeatable helpers for current handlers. There is no public request
streaming API yet.

The V8 bridge may back request, header, body, and signal helpers with cached prototypes or
lazy internal snapshots. Handlers must rely on the documented methods and values, not on
whether helper functions are own properties, when header entries are materialized, or when
body text is decoded.

## Error And Diagnostic Contract

Malformed heads, unsupported versions, Host policy failures, singleton framing conflicts,
body limits, unsupported body framing, unsupported media types, invalid JSON, timeouts,
disconnects, shutdown, response serialization failures, write failures, and pipelining
attempts map to stable Sloppy diagnostic codes. Default transport error bodies are safe
plain text and do not echo request headers, bodies, tokens, cookies, or native error
objects.

Sensitive values must stay out of diagnostics, doctor/audit output, tests, examples, and
goldens. HTTP server diagnostics must not include `Authorization`, `Cookie`, `Set-Cookie`,
`Proxy-Authorization`, API keys, bearer tokens, request bodies, TLS passphrases, private
key material, native pointers, OpenSSL objects, libuv handles, or socket details.

## Invariants

- Parser outputs borrow request-head storage or the arena supplied by the caller; callers
  must keep that storage alive for the request lifecycle.
- Request lifecycles own their body reader, cancellation state, request ID, and terminal
  state. Cleanup is exactly once across success, error, timeout, cancellation, disconnect,
  shutdown, write failure, and late completion.
- Transport connections own libuv/socket state privately. JS-visible context never
  exposes raw parser state, sockets, libuv handles, V8 values, native pointers, or resource
  internals.
- Response headers and status are immutable after headers start. Streaming descriptors
  validate headers before the first write and then use chunked framing only through the
  native transport writer.
- TLS state stays inside the libuv/OpenSSL platform transport. Sloppy validates config,
  drives handshake cleanup, and moves decrypted bytes into the same HTTP/1.1 lifecycle; it
  does not implement TLS protocol logic, certificate validation, or cryptographic
  primitives.
- Buffered request body helpers are consumed once on `ctx.request.body`; top-level
  helpers remain repeatable until the public request-body contract is widened.

## Deferred Behavior

Deferred HTTP server behavior remains internal follow-up scope:

- HTTP/2, HTTP/3, WebSockets, SSE, static files, reverse proxy behavior, and outbound HTTP
  client changes;
- ALPN, mTLS/client certificate auth, custom certificate validation, TLS backend
  abstraction beyond the current OpenSSL path, and production TLS hardening;
- route-level body/header/timeout policy from Plan metadata;
- trusted proxy configuration and `Forwarded`/`X-Forwarded-*` interpretation;
- request ID adoption from trusted headers and a complete access-event/counter model;
- public mutable `SlopResponse`, `onStarting`, `onCompleted`, public request/response
  stream helpers, and public streaming examples;
- production graceful drain, half-close handling, production edge tuning, and benchmark or
  performance claims;
- broad framework binding, middleware, or DI behavior.

## Evidence Lanes

Default evidence covers parser, backend, dispatch, response, transport, HTTPS loopback,
diagnostics, fuzz seed replay, and bounded localhost transport smoke. V8-gated evidence is
required when the JS request-context or result bridge changes. Optional live provider,
torture/stress, and benchmark lanes must be reported separately and must not be counted as
default pass evidence.

## Internal Examples

The internal HTTP example catalog is `docs/modules/http/examples/README.md`. It names the
current `http-basic`, `http-request-context`, `http-streaming-body`,
`http-streaming-response`, `http-keepalive`, and `http-policy-limits` scenarios and keeps
public API gaps explicit.

## Related Docs

- `docs/project/http-server-architecture.md`
- `docs/modules/http/examples/README.md`
- `docs/project/http-transport-runtime-architecture.md`
- `docs/project/http-client-api-architecture.md`
- `docs/testing-strategy.md`
