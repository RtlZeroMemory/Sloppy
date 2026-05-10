# HTTP runtime

The HTTP layer accepts bytes from a socket, parses them into a Sloppy
request, matches a route, dispatches a handler through the V8 bridge,
and writes a response. HTTP/1.1 and HTTP/2 share the same route/handler
model; protocol-specific code stays in the transport and adapter layers.

## Layout

```text
include/sloppy/
  http.h                 parser + request/response models
  http_dispatch.h        dispatch types + Plan binding
  http_response.h
  http_context.h
  http_backend.h         transport-facing types
  http2_frame.h          HTTP/2 frame parse/write helpers
  http2_hpack.h          HPACK adapter
  http2_session.h        nghttp2-backed session boundary
  http2_mapping.h        HTTP/2 header/body to Sloppy request mapping
  http2_dispatch.h       server-side stream dispatcher

src/core/
  http.c                 parser, method/header decoding
  http_backend.c         backend lifecycle + connection scaffolding
  http_dispatch.c        Plan-backed route table + dispatch
  http_context.c         per-request context construction
  http_response.c        response serialization
  http2_frame.c          frame validation and serialization
  http2_hpack.c          HPACK encode/decode wrapper
  http2_session.c        SETTINGS, streams, HPACK, flow-control session
  http2_mapping.c        pseudo-header validation and request lifecycle build
  http2_dispatch.c       stream-to-handler dispatch and response submission
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

HTTP/2 enters the same dispatch path after `http2_dispatch.c` maps a completed
stream into `SlHttpRequestLifecycle`. `:method`, `:scheme`, `:authority`,
`:path`, regular headers, and DATA bytes are validated before dispatch. The
response descriptor is translated back into HTTP/2 HEADERS/DATA frames by the
HTTP/2 dispatcher; handlers do not see a different API.

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

- `max-connections` — admission limit for accepted connections
- `max-active-requests` — backend request slots
- `connection-capacity` — accepted-connection table allocation; must be at
  least `max-connections`
- `max-request-head-bytes` — HTTP/1 request head bytes
- `max-request-body-bytes` — hard ceiling per decoded request body
- `max-request-wire-body-bytes` — raw body bytes retained while waiting for a
  complete HTTP/1 request
- `request-arena-bytes` — per-connection request arena storage
- `max-response-bytes` — fixed HTTP response serialization buffer
- `max-pending-write-bytes` — transport write/backpressure guard
- `http2-max-streams` — HTTP/2 stream concurrency; omitted derives from active
  request slots
- `dispatch-on-event-loop` — HTTP/1 request dispatch is queued to the platform
  loop dispatch phase instead of running inline from the read/parser callback
- `max-dispatches-per-tick` — maximum queued HTTP/1 dispatches drained in one
  loop tick
- `max-target-length` — request target string limit
- `max-query-params` — bounded query parameter count
- `request-timeout-ms` — per-request deadline
- `keep-alive-idle-timeout-ms` — idle timeout between requests on a
  connection
- `max-requests-per-connection` — optional keep-alive cap; `0` disables the cap

These are baked into the Plan from `appsettings.{Environment}.json` /
the env layer. Per-route limits are not surfaced today.

Queued HTTP/1 dispatch still runs on the runtime owner thread. It is a loop
fairness boundary, not a worker-thread or isolate-pool boundary; V8 entry keeps
the owner-thread rules from the V8 bridge.

## Connection Models

HTTP/1.1 keep-alive is sequential: a single connection processes one request at
a time, then either closes or waits for the next request up to the idle
timeout. HTTP/1.1 pipelining is not implemented.

HTTP/2 server connections are multiplexed and experimental; this path is not
yet hardened for production edge use. TLS listeners enter HTTP/2 only through
ALPN `h2`; cleartext listeners accept h2c prior knowledge only on a fresh
connection and accept HTTP/1.1 Upgrade to h2c. Upgrade requests with a body are
handled by nghttp2's upgrade contract: the upgraded stream is request stream 1
and body bytes are not treated as an HTTP/1.1 request body after the protocol
switch. A keep-alive HTTP/1.1 connection cannot switch to h2 by later sending
the prior-knowledge preface.

The HTTP/2 dispatcher keeps stream state separate from the HTTP backend
connection state so multiple streams can have independent request lifecycles on
one TCP/TLS connection. SETTINGS, HPACK, RST_STREAM, GOAWAY, and flow-control
windows are owned by the HTTP/2 session adapter. Server push is disabled.

## Response writing

For HTTP/1.1, `sl_http_response_write` serializes fixed response descriptors
into `connection->response_storage`, then the transport writes that
per-connection buffer. Headers are normalized to lowercase; `Content-Length` is
computed from the body. Streaming responses are transport-owned and use bounded
HTTP/1.1 chunked frames.

For HTTP/2, the dispatcher submits response HEADERS and DATA for the stream.
HTTP/2 does not use HTTP/1.1 chunked framing or connection-specific headers.
Malformed HTTP/2 input sends a protocol shutdown frame when possible and then
closes; the transport never serializes an HTTP/1.1 error response on an
established h2 connection.

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

TLS listeners advertise `h2` and `http/1.1` when the HTTP/2 dispatcher is
configured. ALPN `h2` selects the HTTP/2 path; ALPN `http/1.1` or no ALPN stays
on the HTTP/1.1 path and rejects an h2 preface. Cleartext connections can use
h2c prior knowledge on the first bytes or a valid h2c Upgrade request.

mTLS, custom verification callbacks, OCSP stapling, and HSTS are not
implemented. For production, terminate TLS at a reverse proxy unless this
development listener's TLS posture is sufficient for your lane.

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
- **HTTP/2 transport lanes** cover h2c prior knowledge, h2c Upgrade, TLS ALPN
  `h2` selection, strict pseudo-header mapping, protocol-error close behavior,
  checked allocation sizes, and bounded stream/event lifetime.
- **HTTP/2 protocol unit tests** cover frame validation, HPACK/session adapter
  behavior, request mapping, stream reset/GOAWAY handling, and dispatch.
- **External HTTP/2 conformance** runs full h2spec against a live Sloppy h2c
  transport server in the Linux clang CI lane. The wrapper reports curl,
  nghttp, and h2load smoke lanes separately; those count as coverage only when
  the corresponding lane prints `PASS`, not when the tool is missing or
  unavailable.

## Not implemented

- HTTP/3, gRPC, WebTransport, WebSockets, SSE.
- Streaming request or response APIs in JS.
- Multipart/form-data and file uploads.
- Per-route limits, trusted proxy / forwarded-header policy beyond
  basic header passthrough.
- HTTP/1.1 pipelining.
- Server push public API or server push frames.
