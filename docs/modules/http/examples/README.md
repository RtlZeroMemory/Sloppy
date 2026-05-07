# HTTP Internal Examples

Status: internal HTTP-SERVER-01 examples catalog. These examples document the current
implemented runtime and conformance fixtures. They are not public alpha documentation.

## http-basic

Implemented lane: `core.http.transport`, `conformance.transport.localhost_mvp`, and
`conformance.http.default_dispatch`.

Current behavior: a localhost HTTP/1.1 request with a valid request line, exactly one
non-empty `Host` header, supported method, and matched route receives a fixed response.
Malformed heads, route misses, method mismatches, and dispatch failures use deterministic
error mapping and safe response bodies.

## http-request-context

Implemented lane: `examples/request-context`, `sloppy.run.once_request_context`, and
V8-gated `conformance.v8.http_dispatch_execution`.

Current behavior: V8 handlers can observe protocol-abstract `ctx.request`, `ctx.route`,
`ctx.query`, `ctx.connection`, `ctx.signal`, and `ctx.deadline` fields without raw parser,
socket, libuv, V8, or native handle exposure. Transport-backed dispatch provides request
and connection IDs; synthetic dispatch uses the documented zero IDs.

## http-streaming-body

Implemented lane: bounded request-body materialization through
`conformance.transport.chunked_request` and V8-gated request body helpers.

Current behavior: `Content-Length` and supported chunked request bodies are bounded before
handler entry. V8 exposes consumed-once `ctx.request.body.bytes()`, `text()`, and `json()`
helpers for buffered bodies. Public request streaming is not implemented.

## http-streaming-response

Implemented lane: native transport streaming through
`conformance.transport.streaming_response` and `smoke.transport.keep_alive_streaming_bounded`.

Current behavior: native/runtime streaming descriptors write validated headers once and
then emit HTTP/1.1 chunked body framing. Write-after-terminal and response-capacity paths
are deterministic. Public `SlopResponse.write()`, `writeText()`, `end()`, `onStarting()`,
and `onCompleted()` are not implemented.

## http-keepalive

Implemented lane: `conformance.transport.keep_alive`,
`conformance.transport.keep_alive_idle_timeout`, `conformance.transport.keep_alive_max_requests`,
and `conformance.transport.lifecycle_reset`.

Current behavior: HTTP/1.1 keep-alive reuses one connection for sequential requests only.
`Connection: close`, HTTP/1.0 close policy, idle timeout, max requests per connection, and
cleanup-on-close are deterministic. Pipelining is rejected rather than queued.

## http-policy-limits

Implemented lane: parser/backend/transport unit tests and `sloppy run` server config
metadata.

Current behavior: configured host/port, max connections, max request body bytes, request
timeout, keep-alive, max requests per connection, and idle timeout are consumed by the
runtime. Parser limits cover request line length, header count, header bytes, body bytes,
Host policy, singleton framing conflicts, unsupported versions, unsupported media, and
unsupported transfer codings. Route-level policy metadata, trusted proxy handling,
trusted request ID adoption, and the complete access event model are deferred.
