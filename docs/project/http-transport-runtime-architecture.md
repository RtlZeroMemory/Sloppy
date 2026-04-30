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

Omitted config uses `127.0.0.1`, port `5173`, default backend connection/request caps, and
a bounded backlog. Tests may pass port `0` for an OS-selected localhost port and read it
through `sl_http_transport_server_bound_port`. Invalid host/address, port above `65535`,
zero connection/request capacity, and invalid backlog fail before serving work.

These are foundation defaults, not production-edge defaults.

## Accept Lifecycle

The listen callback claims a slot from the fixed connection table, admits one backend
connection, initializes an internal TCP handle, accepts the pending socket, and parks the
connection in `ACCEPTED`. No request read loop is started in ENGINE-24.A/B.

When capacity is full, the transport accepts the pending socket into an internal overflow
handle and closes it immediately. This prevents unbounded queue growth without pretending to
write a `503` response before the response write loop exists.

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
`REQUEST_READY`. The request is parked on the transport connection and exposed only through
an internal request-ready callback/test hook. ENGINE-24.C does not call route dispatch,
does not enter V8, and does not write a response. #415 owns consuming the parked request,
dispatching it, and writing/closing the response.

Connections can be closed directly, by server stop, by client disconnect, by read/parse/body
failure, or by dispose. Cleanup stops reads before closing the handle, closes any unfinished
body reader, closes the request lifecycle, and releases backend connection/request
admission exactly once. A disconnect while reading the head or body produces a deterministic
connection-closed diagnostic and does not enter V8.

## Diagnostics

Transport diagnostics use stable Sloppy diagnostic codes:

- `SLOPPY_E_HTTP_TRANSPORT_CONFIG`;
- `SLOPPY_E_HTTP_BIND_FAILED`;
- `SLOPPY_E_HTTP_LISTEN_FAILED`;
- `SLOPPY_E_HTTP_ACCEPT_FAILED`;
- `SLOPPY_E_HTTP_CONNECTION_CLOSED` for read errors or client disconnect;
- `SLOPPY_E_HTTP_HEADER_BYTES_LIMIT` for oversized accumulated request heads;
- `SLOPPY_E_INVALID_HTTP_REQUEST` for malformed heads or invalid Content-Length;
- `SLOPPY_E_HTTP_UNSUPPORTED_BODY` for Transfer-Encoding/chunked;
- `SLOPPY_E_HTTP_BODY_LIMIT` for oversized bodies;
- `SLOPPY_E_HTTP_UNSUPPORTED_MEDIA_TYPE` for unsupported body media;
- `SLOPPY_E_HTTP_KEEP_ALIVE_UNSUPPORTED` for detected pipelined request bytes;
- existing `SLOPPY_E_HTTP_OVERLOAD` for backend admission pressure;
- existing `SLOPPY_E_APP_LIFECYCLE` for lifecycle misuse.

User-facing messages do not include libuv handles, raw pointers, or socket internals.
