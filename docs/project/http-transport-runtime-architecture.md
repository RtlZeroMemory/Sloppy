# HTTP Transport Runtime Architecture

## Boundary

The HTTP transport boundary is Slop-owned. Public/internal runtime code sees
`SlHttpTransportServer`, `SlHttpTransportConfig`, and `SlHttpTransportConnection`.
Platform objects are opaque `SlHttpPlatformListener` and `SlHttpPlatformConnection`
pointers. `uv_loop_t`, `uv_tcp_t`, and libuv callback details live only in
`src/platform/libuv/http_transport_libuv.c`.

The transport composes the existing `SlHttpBackend` instead of replacing it. The backend
continues to own admission counters and core connection/request lifecycle state. The
transport owns listener handles, accepted TCP placeholders, and bounded connection storage.

## Server Lifecycle

```text
created -> listening -> stopping -> stopped -> disposed
created -> stopped -> disposed
created/listening -> error -> stopped -> disposed
```

`init` validates and copies config into arena-owned storage. `listen` validates the bind
address, initializes the internal libuv loop/listener, binds host/port, starts listening,
and starts the core backend. Double listen fails with a deterministic lifecycle diagnostic.
Stopping before listen is safe and reaches `STOPPED`. Stop after listen closes accepted
placeholder connections, closes listener resources, drains pending close callbacks, and
stops the backend. Dispose stops as needed and then disposes backend state.

## Config

Omitted config uses `127.0.0.1`, port `5173`, default backend connection/request caps, a
bounded backlog, bounded per-connection response storage, and bounded transport timers for
header read, body read, total request, and response write phases. Tests may pass port `0`
for an OS-selected localhost port and read it through `sl_http_transport_server_bound_port`.
Invalid host/address, port above `65535`, zero connection/request/response capacity, and
invalid backlog fail before serving work. Timer values of zero use the bounded defaults;
there is no public idle-timeout or production tuning surface yet.

These are foundation defaults, not production-edge defaults.

## Accept Lifecycle

The listen callback claims a slot from the fixed connection table, admits one backend
connection, initializes an internal TCP handle, accepts the pending socket, and parks the
connection in `ACCEPTED`. No request read loop is started in ENGINE-24.A/B.

When capacity is full, the transport accepts the pending socket into an internal overflow
handle and closes it immediately. This prevents unbounded queue growth without pretending to
write a `503` response before a connection slot and response buffer are available.

## Read Loop And Request Accumulation

ENGINE-24.C starts the libuv read loop as soon as an accepted connection is admitted into
the bounded connection table. Each connection owns fixed transport storage for read chunks,
request-byte accumulation, and a request arena. Incoming TCP chunks append through the core
bounded byte-builder primitive; the transport never grows an unbounded socket buffer and
never exposes libuv handles or native pointers to user-facing diagnostics.

The transport scans accumulated bytes only far enough to find `CRLFCRLF` and to read the
`Content-Length`/`Transfer-Encoding` header policy needed before the complete-buffer parser
can run. Once the required bytes are present, the existing ENGINE-13 parser/body-reader path
owns semantic validation: parser target/header limits, malformed-head diagnostics,
Content-Length body length, body-size limit, and JSON/text media policy. Parsed target,
path, headers, and body bytes remain request-arena owned.

Supported request framing is intentionally small:

- one request per connection;
- `Content-Length` bodies only;
- empty bodies are supported;
- body bytes may arrive in the same TCP chunk as the head or across later chunks;
- `Transfer-Encoding`/chunked, streaming bodies, keep-alive, and pipelining are rejected
  or closed as unsupported MVP behavior.

When a full request is parsed and the body reader finishes, the connection transitions to
`REQUEST_READY`. ENGINE-24.D consumes that state exactly once when a dispatch callback is
configured: backend request state moves from reading to dispatching, the callback returns a
normal `SlHttpResponse`, the existing response writer serializes bytes into the
connection-owned response buffer, libuv writes those bytes, and the connection closes after
the write callback. The optional request-ready hook remains for tests/observation, but it
is no longer the only consumer of parsed requests.

The MVP connection policy is still one request per connection. Extra/pipelined bytes are
rejected before dispatch, keep-alive is not enabled, response bodies are serialized
eagerly, and streaming/chunked response writing is not implemented.

## Keep-Alive Decision And HTTP/1.1 Upgrade Plan

ENGINE-24.G keeps the ENGINE-24 MVP close-after-response by design:

- one request is served per TCP connection;
- the response writer emits `Connection: close`;
- the transport closes the connection after write completion;
- explicit `Connection: keep-alive` does not keep the connection open;
- a sequential second request on the same connection is not accepted;
- pipelined bytes after the first complete request are rejected as unsupported MVP
  behavior.

That decision is intentional rather than an unfinished hidden feature. Close-after-response
keeps cleanup direct, avoids keeping request arenas and body-reader state alive for idle
connections, reduces timeout and shutdown race surfaces, and is enough for the localhost
transport proof and users API proof planned after the core transport MVP. It also keeps the
current connection terminal path easy to audit: read one bounded request, dispatch once,
write one bounded response, then close and release backend connection/request admission
exactly once.

Keep-alive is deferred until a later HTTP/1.1 upgrade slice can implement and test the full
connection loop. That later work must define at least:

- sequential requests per connection, with the read loop resuming only after response write
  completion;
- an idle timeout for connections that are open between requests;
- a maximum requests-per-connection cap;
- shutdown drain behavior for idle, reading, dispatching, and writing keep-alive
  connections;
- parser, accumulated-byte, body-reader, response, and request-lifecycle state reset for
  each request;
- request-arena reset or equivalent per-request storage rotation between requests;
- per-request cancellation and timeout state that is independent from the longer-lived TCP
  connection;
- diagnostics for idle timeout, max-requests close, client close while idle or between
  requests, and shutdown drain/force-close.

HTTP/1.1 pipelining is not part of the MVP and is likely to remain unsupported or
deprioritized. If Slop ever supports pipelining, response ordering, queued response
ownership, cancellation behavior for queued requests, and bounded queue limits must be
explicitly specified and tested before enabling it. HTTP/2 is the later multiplexing
direction; pipelining must not become an accidental substitute for that design.

Chunked request decoding, chunked response encoding, and streaming response bodies are also
deferred. Future streaming work must define socket backpressure behavior, body-stream
lifetime, cancellation and shutdown semantics for partially consumed streams, and memory
ownership for data that outlives one parser callback. ENGINE-24.G does not implement any of
that behavior.

Future issue recommendation only: create a later `ENGINE-25: HTTP/1.1 Keep-Alive and
Streaming` epic after the close-after-response core proof lands. Likely tasks are:

- keep-alive connection loop;
- idle timeout and max requests per connection;
- sequential request lifecycle reset;
- chunked request decoding;
- chunked/streaming response writer;
- keep-alive stress and conformance.

No ENGINE-25 GitHub issues are created by ENGINE-24.G.

If no dispatch callback is configured, the parsed request is closed immediately so backend
admission is released rather than parked forever. ENGINE-24.F/#417 adds real localhost TCP
smoke over the reusable transport using raw client request bytes and native/fake dispatch
callbacks. That evidence proves bounded MVP transport behavior for simple success, route
miss, method mismatch, POST text body handling, malformed input, body limits, unsupported
media, unsupported transfer encoding, one request per connection, close-after-response, and
shutdown cleanup. It does not claim V8 transport execution, benchmark/performance
evidence, production graceful-drain behavior, or production-edge HTTP readiness.

ENGINE-24.E adds the transport terminal-state layer. Connections can be closed directly, by
server stop, by client disconnect, by read/parse/body failure, dispatch failure, timeout,
write completion, write failure, or dispose. Cleanup stops reads and timers before closing
the handle, preserves the response buffer until the libuv write callback, closes any
unfinished body reader, closes/cancels/times out the request lifecycle when one exists, and
releases backend connection/request admission exactly once. A disconnect while reading the
head or body produces a deterministic connection-closed diagnostic, cancels any active
request, closes the connection, and does not write a handler-failure response. Timeout
callbacks are owner-loop libuv timers; header/body/total-request timeouts write a
deterministic `408 Request Timeout` response when the connection is still writable,
otherwise they close. Write timeout/failure marks the request terminal and treats later
write completion as cleanup-only. No V8 work runs from read, timer, or write callbacks.

Server shutdown is an immediate-cancel/drain-lite MVP policy. `stop` stops accepting,
rejects any accepted-after-shutdown work through the overflow close path, moves the backend
to stopping, cancels active request work through the existing shutdown token path when a
request lifecycle exists, closes idle/reading/dispatching/writing connections, drains close
callbacks, and disposes listener/server state exactly once. It is not a production graceful
drain implementation: there is no configurable drain window, half-close policy, idle
keep-alive pool, signal integration, or production edge claim.

## Diagnostics

Transport diagnostics use stable Sloppy diagnostic codes:

- `SLOPPY_E_HTTP_TRANSPORT_CONFIG`;
- `SLOPPY_E_HTTP_BIND_FAILED`;
- `SLOPPY_E_HTTP_LISTEN_FAILED`;
- `SLOPPY_E_HTTP_ACCEPT_FAILED`;
- `SLOPPY_E_HTTP_CONNECTION_CLOSED` for read errors or client disconnect;
- `SLOPPY_E_HTTP_REQUEST_TIMEOUT` for header, body, and total-request transport timeouts;
- `SLOPPY_E_HTTP_HEADER_BYTES_LIMIT` for oversized accumulated request heads;
- `SLOPPY_E_INVALID_HTTP_REQUEST` for malformed heads or invalid Content-Length;
- `SLOPPY_E_HTTP_UNSUPPORTED_BODY` for Transfer-Encoding/chunked;
- `SLOPPY_E_HTTP_BODY_LIMIT` for oversized bodies;
- `SLOPPY_E_HTTP_UNSUPPORTED_MEDIA_TYPE` for unsupported body media;
- `SLOPPY_E_HTTP_KEEP_ALIVE_UNSUPPORTED` for detected pipelined request bytes;
- `SLOPPY_E_HTTP_DISPATCH_FAILED` for missing/invalid transport dispatch wiring;
- `SLOPPY_E_HTTP_RESPONSE_SERIALIZATION_FAILED` for response writer/buffer-capacity
  failures;
- `SLOPPY_E_HTTP_WRITE_FAILED` for write-start, write-timeout, or write-completion
  failures;
- `SLOPPY_E_HTTP_CLOSE_FAILED` is reserved for detectable close-after-write lifecycle
  failures;
- existing `SLOPPY_E_HTTP_OVERLOAD` for backend admission pressure;
- existing `SLOPPY_E_APP_LIFECYCLE` for lifecycle misuse.

User-facing messages do not include libuv handles, raw pointers, or socket internals.
