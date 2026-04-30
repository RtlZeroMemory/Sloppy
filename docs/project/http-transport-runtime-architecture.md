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

Connections can be closed directly, by server stop, or by dispose. Cleanup is idempotent at
the Slop lifecycle level and does not expose native handles or pointer values.

## Diagnostics

Transport diagnostics use stable Sloppy diagnostic codes:

- `SLOPPY_E_HTTP_TRANSPORT_CONFIG`;
- `SLOPPY_E_HTTP_BIND_FAILED`;
- `SLOPPY_E_HTTP_LISTEN_FAILED`;
- `SLOPPY_E_HTTP_ACCEPT_FAILED`;
- existing `SLOPPY_E_HTTP_OVERLOAD` for backend admission pressure;
- existing `SLOPPY_E_APP_LIFECYCLE` for lifecycle misuse.

User-facing messages do not include libuv handles, raw pointers, or socket internals.
