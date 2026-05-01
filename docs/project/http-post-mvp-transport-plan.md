# HTTP Post-MVP Transport Plan

Status: source of truth for #433 and HTTP-25 tasks. HTTP-25.A/B/C are implemented as a
bounded sequential keep-alive upgrade; chunked request decoding, response streaming, and
stress/conformance remain separate follow-ups.

## Current HTTP MVP

| Behavior | Current status |
| --- | --- |
| Request model | Sequential HTTP/1.1 keep-alive; one request is dispatched at a time per connection. |
| Connection close | HTTP/1.1 keeps alive by default unless disabled, `Connection: close` is present, shutdown starts, an unsafe error occurs, or max requests is reached. |
| Request body framing | Content-Length only. |
| Transport scope | Bounded localhost transport. |
| Claims | No production-edge, benchmark, TLS, HTTP/2, HTTP/3, WebSocket, pipelining, chunked, or streaming claims. |

## HTTP-25.A/B/C Implemented Semantics

- HTTP/1.1 defaults to sequential keep-alive. `Connection: close`, keep-alive-disabled
  config, HTTP/1.0 requests, shutdown, unsafe error responses, and max-request exhaustion
  force `Connection: close` and close after the response write.
- Keep-alive is sequential only. The transport does not dispatch a second request until
  the first response write callback completes and request-owned state has been reset.
- Pipelined bytes already buffered before the current response completes are rejected with
  a deterministic pipelining diagnostic and the connection closes.
- Request-owned arena state, parsed head, headers, body reader, body bytes, response
  buffer metadata, cancellation token, diagnostics, and active-request admission are reset
  between sequential keep-alive requests. Socket handle, connection id, request count,
  server/backend references, counters, timers, and bounded connection buffers remain
  connection-owned.
- Idle timeout starts after a keep-alive response is fully written and the connection
  returns to idle/read-wait. New request bytes stop the idle timer. Timeout closes the
  connection cleanly once.
- Max requests are counted per connection. When the configured maximum is reached, the
  final response is written with `Connection: close` and the connection closes after write.
- Shutdown stops accepting, closes idle keep-alive connections, lets an already active
  response follow the existing drain-lite policy, and prevents new keep-alive requests.

Configuration keys emitted by the compiler and consumed by `sloppy run --artifacts`:

| Key | Default | Behavior |
| --- | ---: | --- |
| `Sloppy:Server:KeepAliveEnabled` | `true` | `false` disables reuse and restores close-after-response behavior. |
| `Sloppy:Server:KeepAliveIdleTimeoutMs` | `5000` | Positive idle timeout after a keep-alive response write completes. |
| `Sloppy:Server:MaxRequestsPerConnection` | `100` | Positive per-connection completed-request cap. |

## Next HTTP/1.1 Work

- #433 owns HTTP/1.1 keep-alive and streaming.
- #441 defines the keep-alive state machine and sequential connection loop. Implemented for
  bounded sequential HTTP/1.1.
- #442 adds idle timeout and max requests per connection. Implemented with Plan/server
  configuration keys above.
- #443 defines request lifecycle reset between sequential requests. Implemented for the
  current buffered request/response transport path.
- #444 adds chunked request decoding.
- #445 adds streaming response writer behavior.
- #446 adds stress/conformance evidence for keep-alive and streaming.

## Later

- TLS and reverse-proxy awareness.
- WebSockets.
- HTTP/2.
- HTTP/3 research.
- Production graceful drain/hardening if explicitly scoped.

## Non-Goals For Immediate Next Wave

- No TLS, HTTP/2, HTTP/3, or WebSockets unless the owner explicitly chooses that scope.
- No production HTTP claims.
- No benchmark/performance claims.
- No Node compatibility or package-manager behavior.
