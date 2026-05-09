# HTTP runtime

The HTTP layer accepts bytes from a socket, parses them into a Sloppy
request, matches a route, dispatches a handler through the V8 bridge,
and writes a response. It's HTTP/1.1 only and aimed at the development
loop — not a production-edge server.

## Layout

```text
include/sloppy/
  http.h                 parser + request/response models
  http_dispatch.h        dispatch types + Plan binding
  http_response.h
  http_context.h
  http_backend.h         transport-facing types

src/core/
  http.c                 parser, method/header decoding
  http_backend.c         backend lifecycle + connection scaffolding
  http_dispatch.c        Plan-backed route table + dispatch
  http_context.c         per-request context construction
  http_response.c        response serialization
  request_validation.c   body/content-type/limit gates
  route.c                pattern parser + match
src/platform/libuv/
  http_transport_libuv.c connection + read/write driven by libuv
```

## Request flow

```text
libuv accept                       http_transport_libuv.c
   ▼
read bytes into a per-connection arena
   ▼
sl_http_request_parse              http.c
   │  validate request line, headers, content-length / framing
   ▼
sl_http_dispatch_dispatch          http_dispatch.c
   │  route_table lookup (literal-before-param, source order)
   │  method match
   │  request_validation.c: body kind, JSON validity, body size
   ▼
build SlHttpContext                http_context.c
   │  route params, query (last-wins), headers, body helpers
   ▼
open per-request scope             scope.c
   │
   ▼
engine dispatch (handler ID)       src/engine/v8/* via engine_dispatch
   ▼
result descriptor (Results.*)
   │
   ▼
sl_http_response_write             http_response.c
   ▼
libuv write                        http_transport_libuv.c
   ▼
end scope, run scope cleanups      scope.c
```

## Route table

Built once at startup from validated Plan routes. Entries are sorted in
this order:

1. Literal patterns before parameter patterns.
2. Within each group, source order.

Lookup walks the table in order — the first match wins. Pattern
matching is in `sl_route_pattern_match` (`src/core/route.c`); it
supports literal segments and `{name}` / `{name:int}` parameters with
strict trailing-slash behavior.

The table is read-only at request time; a registered route never
disappears or moves.

## Body / content-type policy

`request_validation.c` enforces, before any handler runs:

| Failure mode                                    | Status |
| ----------------------------------------------- | ------ |
| Method not in `GET/HEAD/POST/PUT/PATCH/DELETE` | 405    |
| Body declared but `Content-Type` missing        | 415    |
| `Content-Type` not supported                    | 415    |
| Body exceeds configured limit                   | 413    |
| `Expect` header present                         | 417    |
| Malformed JSON for `application/[*+]json`       | 400    |

Supported request media types today are `application/json`,
`application/*+json`, `text/plain`, and `application/octet-stream`.
The transport decodes bounded `Transfer-Encoding: chunked` request
bodies before validation. Unsupported transfer codings, trailers,
invalid chunk framing, and `Transfer-Encoding` plus `Content-Length`
conflicts are rejected before handler dispatch.

The runtime owns the body — the parser allocates it inside the
per-request arena. JS body helpers (`request.text()`, `request.json()`)
return copies into V8-owned storage.

Incoming `HEAD` requests match the corresponding `GET` route. Dispatch
still executes the handler so validation and metadata stay identical to
GET, but the transport writes only the response head and preserves the
computed `Content-Length`.

`405 Method Not Allowed` responses from the Plan-backed run path include an
`Allow` header when the route table can match the request path. The header
lists route-backed methods in deterministic order and includes `HEAD` when
`GET` is available for that path.

`Expect: 100-continue` is not negotiated today. The transport rejects
requests carrying an `Expect` header with `417 Expectation Failed` before
waiting for the request body or entering handler dispatch.

## Limits

Server-wide limits are read from Plan-emitted server config:

- `max-connections` — bounded pool of in-flight connections
- `max-request-body-bytes` — hard ceiling per request body
- `max-target-length` — request target string limit
- `max-query-params` — bounded query parameter count
- `request-timeout-ms` — per-request deadline
- `keep-alive-idle-timeout-ms` — idle timeout between requests on a
  connection
- `max-requests-per-connection` — keep-alive cap

These are baked into the Plan from `appsettings.{Environment}.json` /
the env layer. Per-route limits are not surfaced today.

## Keep-alive

Keep-alive is sequential: a single connection processes one request at
a time, then either closes or waits for the next request up to the idle
timeout. There's no pipelining and no HTTP/2.

## Response writing

`sl_http_response_write` serializes fixed response descriptors into a
single buffer inside the per-request arena, then hands them to the
transport for write. Headers are normalized to lowercase; `Content-Length`
is computed from the body. Streaming responses are transport-owned and
use bounded HTTP/1.1 chunked frames.

`204 No Content` and `304 Not Modified` never write `Content-Type`,
`Content-Length`, or body bytes even if the handler descriptor includes a
body.

If the descriptor is malformed (bad status code, body kind that doesn't
match content type, oversized headers), the dispatcher logs a
diagnostic and the connection responds 500 with a redacted body.

## TLS

Inbound TLS is opt-in OpenSSL plumbing in
`src/platform/libuv/http_transport_libuv.c`. It wraps the libuv socket
with an OpenSSL `BIO` pair when the server config enables it.
Configuration:

Configuration keys live under `sloppy:server:tls:*` (env-var form
`SLOPPY__SERVER__TLS__ENABLED`, etc.):

```
sloppy:server:tls:enabled        = true
sloppy:server:tls:certificatePath = path/to/cert.pem
sloppy:server:tls:privateKeyPath  = path/to/key.pem
```

ALPN negotiation is HTTP/1.1-only; mTLS, custom verification, HSTS are
not implemented. For production, terminate TLS at a reverse proxy.

## Cancellation and shutdown

A request scope carries a deadline (from `request-timeout-ms` plus any
provider-level overrides). Expiration cancels in-flight provider work
and queues the request for cleanup; the handler sees a cancellation
diagnostic if it tries to read more bytes after that point.

Server shutdown:

1. Stops accepting new connections.
2. Lets in-flight requests run to their deadlines or completion.
3. Drains scope cleanups (provider close, transient services).
4. Releases the engine.

This is "drain-lite" — production-grade graceful drain (long timeout,
connection draining) is the responsibility of an in-front reverse proxy.

## Tests

- **Parser unit tests** under `tests/unit/core/test_http*.c` cover
  request line decoding, header parsing, body framing.
- **Dispatch tests** under `tests/integration/http_dispatch/` exercise
  the full Plan-backed dispatch path with synthetic requests.
- **Conformance** runs `--once` requests through the real CLI for
  golden response checking.
- **Transport tests** exercise the running libuv listener, keep-alive,
  `HEAD`, `405`, `417`, TLS configuration, and response serialization.
- **V8-gated run lanes** execute compiled handlers through `sloppy run --once`.
  Listener-to-V8 socket coverage is tracked separately from synthetic
  `--once` dispatch.

## Not implemented

- HTTP/2, HTTP/3, WebSockets, SSE.
- Streaming request or response APIs in JS.
- Multipart/form-data and file uploads.
- Per-route limits, trusted proxy / forwarded-header policy beyond
  basic header passthrough.
- Pipelining.
