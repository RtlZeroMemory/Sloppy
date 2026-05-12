# Network

`sloppy/net` is the bootstrap stdlib network surface. It exposes the
outbound HTTP client, raw TCP client/listener primitives, local IPC
endpoints (Unix sockets / Windows named pipes), and a small
`NetworkAddress` value type.

## Import

```ts
import {
    HttpClient,
    TcpClient,
    TcpListener,
    TcpConnection,
    LocalEndpoint,
    UnixSocket,
    NamedPipe,
    NetworkAddress,
    SloppyNetError,
} from "sloppy/net";
```

The compiler recognizes `sloppy/net` as a stdlib subpath. Compiler source
input accepts all names in the import example above. Importing `HttpClient`
emits the HTTP client feature; importing TCP, local IPC, address, or error
symbols emits the `stdlib.net` runtime feature into the Plan.

## Current status

This public alpha, pre-production API shape is committed for current experiments. All
operations require the
`__sloppy.net` runtime bridge; without it the first call rejects with
`SLOPPY_E_UNAVAILABLE_RUNTIME_FEATURE` (or the matching feature-specific
unavailability code).

There is no standalone DNS API, no UDP/QUIC, and no WebSocket client. Inbound
HTTP request handling is part of the framework (`Sloppy.create()` routing),
not `sloppy/net`.

## HTTP client

`HttpClient` is the outbound HTTP client. It uses HTTP/1.1 by default and
supports explicit h2/h2c requests. It has its own page —
[HTTP Client](http-client.md) — covering static helpers, the
`HttpClient.create(...)` factory, request/response shape, redirect policy,
protocol selection, TLS option boundaries, and error codes.

`HttpClient` is exported both from `"sloppy"` (for app code) and from
`"sloppy/net"` (for compiler source input that needs the network feature
recorded in the Plan).

## TCP

### `TcpClient`

```ts
import { TcpClient } from "sloppy/net";

const conn = await TcpClient.connect({
    host: "example.invalid",
    port: 9000,
    noDelay: true,
    timeoutMs: 5000,
    keepAlive: { enabled: true, delayMs: 30000 },
});
```

Options:

| Option | Notes |
| --- | --- |
| `host` | non-empty string, required |
| `port` | 1..65535, required |
| `noDelay` | optional boolean (TCP_NODELAY) |
| `timeoutMs` | optional non-negative connect timeout |
| `keepAlive` | optional `{ enabled: boolean, delayMs?: number }` |

`TcpClient.connect` resolves to a `TcpConnection`.

### `TcpListener`

```ts
const listener = await TcpListener.listen({
    host: "127.0.0.1",
    port: 0,            // 0 = ephemeral
    backlog: 64,
});

for await (const conn of listener) {
    // handle conn
}
```

| Option | Notes |
| --- | --- |
| `host` | non-empty string, required |
| `port` | 0..65535, required (`0` = ephemeral) |
| `backlog` | optional positive integer |

The listener exposes:

- `accept(options?) → Promise<TcpConnection>`
- `acceptLoop(options?) → AsyncIterable<TcpConnection>`
- `[Symbol.asyncIterator]()` — sugar for `acceptLoop`
- `close()` / `abort()`
- `closed` getter

### `TcpConnection`

```ts
await conn.writeText("ping\n");
const line = await conn.readLine();
await conn.close();
```

| Method | Returns |
| --- | --- |
| `conn.write(bytes)` | `Promise<void>` |
| `conn.writeText(text)` | `Promise<void>` |
| `conn.read(options?)` | `Promise<Uint8Array>` (default chunk 8 KiB; `maxBytes` 1..65536) |
| `conn.readUntil(delimiter, options?)` | `Promise<Uint8Array>` |
| `conn.readLine(options?)` | `Promise<Uint8Array>` (splits on `\r\n`, strips the delimiter) |
| `conn.readChunks(options?)` | `AsyncIterable<Uint8Array>` |
| `conn.close()` / `conn.abort()` | `Promise<void>` |
| `conn.closed` | boolean |

`readChunks(...)` is a JavaScript async-iterator helper over repeated
`read(...)` calls. It is not the native Core stream foundation and does not
expose a transferable or Node-compatible stream handle.

## Local IPC

`LocalEndpoint` covers Unix sockets (`backend: "unix"`) and Windows named
pipes (`backend: "namedPipe"`) with the same connection/listener shape as
TCP. `UnixSocket` and `NamedPipe` are convenience facades that pin the
backend. These names are public in both app-host JavaScript and compiler
source input.

```ts
import { LocalEndpoint } from "sloppy/net";

const server = await LocalEndpoint.listen({
    path: "runtime:/sock.sloppy",
    unlinkExisting: true,
    permissions: "0600",          // Unix-only, octal string
});

for await (const conn of server) {
    // handle conn
}
```

Path constraints:

- Path must be a named-root path (`root:/segment`).
- Root names match `[A-Za-z][A-Za-z0-9_.-]*`.
- Segments are alphanumeric plus `.` and `-`. No `..`, `/`, `\`, or NUL.

`backend` defaults to `"unix"` on POSIX and `"namedPipe"` on Windows. Force
the backend with `UnixSocket.connect(...)` / `NamedPipe.connect(...)` or by
passing `backend: "unix" | "namedPipe"` explicitly.

`LocalEndpoint.connect(...)` and the connections returned by `accept()`
expose the same read/write surface as `TcpConnection` (`write`, `writeText`,
`read`, `readUntil`, `readLine`, `readChunks`, `close`, `abort`, `closed`).
Each accepts `{ timeoutMs, deadline, signal }`.

## NetworkAddress

`NetworkAddress` is an immutable host/port value.

```ts
import { NetworkAddress } from "sloppy/net";

const a = new NetworkAddress("example.invalid", 9000);
const b = NetworkAddress.parse("[::1]:8080");
const c = NetworkAddress.parse({ host: "10.0.0.1", port: 80 });

a.toString();   // "example.invalid:9000"
b.toString();   // "[::1]:8080"
```

`NetworkAddress.parse(value)` accepts a `NetworkAddress`, an
`"host:port"` / `"[ipv6]:port"` string, or a `{ host, port }` object.
Out-of-range ports throw a plain `TypeError`.

## Common options

Read/write/connect/listen/accept calls accept a trailing options object with
timing fields:

```ts
{
  timeoutMs?: number;
  deadline?: Deadline;        // from sloppy/time
  signal?: AbortSignal | CancellationSignal;
}
```

The earliest of `timeoutMs`, `deadline.remainingMs()`, and signal abort wins
and rejects the operation.

## Examples

In-repo references:

- `examples/http-client-basic` — outbound HTTP usage
- `examples/net-tcp-client`, `examples/net-tcp-echo`, `examples/net-tcp-server`
- `examples/net-deadline-cancel` — timeout/deadline behavior
- `examples/net-policy-strict` — origin policy on `HttpClient`
- `examples/net-local-ipc` — `LocalEndpoint` round-trip
- `examples/core-network-time-codec` — net + deadlines + codec

## Boundaries

- HTTP/1.1 is the default for cleartext `http://` URLs. `HttpClient` supports
  explicit `protocol: "h2"` / `protocol: "h2c"`, pooled h2 multiplexing, and
  HTTPS `auto` ALPN h2 selection.
- No DNS API. Hostnames are resolved as part of `connect`/`listen`.
- No UDP, raw IP, QUIC, or SCTP.
- No WebSocket client.
- No server-side framework HTTP. `Sloppy.create()` owns inbound HTTP.
- No `node:net`, `node:tls`, `node:dgram`, or `node:http` compatibility —
  those imports are rejected by the compiler.
- TLS options are accepted only by `HttpClient`; `TcpClient`/`TcpListener`
  / `LocalEndpoint` do not currently expose a TLS configuration. See
  [HTTP Client → Current Boundaries](http-client.md#current-boundaries).

## Compiler source-input support

The compiler accepts `import ... from "sloppy/net"` and emits the
`stdlib.net` required feature. Aliased and default imports are rejected.

## Runtime requirements

`sloppy/net` requires the `__sloppy.net` V8 intrinsic namespace. The bridge
exposes TCP connect/listen/accept/read/write, local-IPC variants, and the
private outbound TLS bridge used by `HttpClient`. Bridge absence reports
`SLOPPY_E_UNAVAILABLE_RUNTIME_FEATURE` rather than half-running. Per-feature
gates produce more specific codes — for example
`SLOPPY_E_NET_LOCAL_IPC_FEATURE_UNAVAILABLE` when the local-IPC subset is
disabled, and `SLOPPY_E_HTTP_CLIENT_TLS_BACKEND_UNAVAILABLE` when the TLS
backend is absent.

## Errors

Errors are subclasses of `SloppyNetError extends Error`. The shape is
`{ name, code, cause? }`, where `code` follows the `SLOPPY_E_*` prefix.

Local IPC codes:

- `SLOPPY_E_NET_LOCAL_IPC_FEATURE_UNAVAILABLE`
- `SLOPPY_E_NET_LOCAL_IPC_LISTEN_FAILED`
- `SLOPPY_E_NET_LOCAL_IPC_CONNECT_FAILED`
- `SLOPPY_E_NET_LOCAL_IPC_ACCEPT_CANCELLED`
- `SLOPPY_E_NET_LOCAL_IPC_READ_WRITE_CANCELLED`

HTTP client codes are listed in [HTTP Client](http-client.md). TCP errors
flow through the same shared error class with descriptive bridge messages.
