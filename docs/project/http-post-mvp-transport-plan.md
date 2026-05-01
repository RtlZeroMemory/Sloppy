# HTTP Post-MVP Transport Plan

Status: planning source of truth for #433 and HTTP-25 tasks. This is post-MVP work, not a
current HTTP proof claim.

## Current HTTP MVP

| Behavior | Current status |
| --- | --- |
| Request model | One request per connection. |
| Connection close | Close after response. |
| Request body framing | Content-Length only. |
| Transport scope | Bounded localhost transport. |
| Claims | No production-edge, benchmark, TLS, HTTP/2, HTTP/3, WebSocket, or keep-alive claims. |

## Next HTTP/1.1 Work

- #433 owns HTTP/1.1 keep-alive and streaming.
- #441 defines the keep-alive state machine and sequential connection loop.
- #442 adds idle timeout and max requests per connection.
- #443 defines request lifecycle reset between sequential requests.
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

