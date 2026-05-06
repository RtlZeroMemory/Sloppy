# Network API Architecture

Status: CORE-NET-01.C/D/H client implementation. This document defines the low-level
TCP/IP runtime API, policy model, feature metadata, diagnostics, and first native TCP
client/connection implementation. It is not execution evidence for TCP listen/accept
behavior, broad DNS policy, external network access, TLS, HTTP client behavior, UDP,
WebSocket, or local IPC.

## Goals

`sloppy/net` gives Sloppy apps a compact runtime-owned API for IPv4/IPv6 TCP clients and
listeners:

- outbound TCP connections;
- inbound loopback/external TCP listeners;
- owned connection resources with deterministic lifecycle transitions;
- text and byte read/write helpers;
- line and delimiter reads;
- bounded buffers, backpressure, deadlines, and cancellation;
- DNS and endpoint metadata that do not block the V8 owner thread;
- Plan-visible network access metadata where the compiler can see it.

The API is intentionally lower-level than HTTP. HTTP client/server, TLS, UDP, WebSocket,
Unix domain sockets, Windows named pipes, Node `net` compatibility, and package-manager
behavior are outside CORE-NET-01.

## Public Module

The public import is:

```ts
import { TcpClient, TcpListener, TcpConnection, NetworkAddress } from "sloppy/net";
```

The compiler recognizes only named, unaliased imports from `sloppy/net`. The import adds
`stdlib.net` to emitted Plan `requiredFeatures[]`, emits `features.network = true`, and
sets `strongPlan.evidence.network = true`. CORE-NET-01.C/D/H makes `stdlib.net`
available when its dependencies are available and installs the initial `__sloppy.net`
client/connection bridge for active V8 plans.

## API Contract

`TcpClient.connect(options)`:

- `host`: string hostname, IPv4 address, or IPv6 address;
- `port`: integer TCP port;
- `timeoutMs`: optional connect timeout;
- `deadline`: optional CORE-TIME deadline;
- `signal`: optional cancellation signal;
- `noDelay`: optional TCP_NODELAY request;
- `keepAlive`: optional `{ enabled: boolean, delayMs?: number }`.

`TcpListener.listen(options)`:

- `host`: string hostname, IPv4 address, or IPv6 address;
- `port`: integer TCP port; `0` requests an ephemeral port;
- `backlog`: optional bounded accept backlog.

`TcpConnection`:

- `write(bytes)` and `writeText(text)` copy caller data into owned write buffers;
- `read(options?)` returns bytes subject to read limits;
- `readLine(options?)` reads through a line terminator;
- `readUntil(delimiter, options?)` reads through a byte delimiter;
- `readChunks(options?)` exposes bounded async iteration over received chunks;
- `close()` closes cleanly once;
- `abort(reason?)` tears down pending work and closes once;
- endpoint metadata exposes safe local/remote address and port values.

`NetworkAddress` parses and formats host/port data without exposing sockets, OS handles,
libuv handles, raw native pointers, or resource internals to JavaScript.

## Lifecycle

Connections transition through:

- `connecting`;
- `connected`;
- `half-closed` where the platform/backend can represent end-write cleanly;
- `closing`;
- `closed`;
- `aborted`;
- `failed`.

Terminal states are explicit. Late native completions after close, abort, timeout,
cancellation, or shutdown are cleanup-only. Stale handles reject deterministically.

`close()` is graceful and idempotent. `abort()` is terminal, rejects pending reads/writes,
and never reports success for work that did not complete. `endWrite()` is a future helper
for backends that support half-close; until implemented it must either be absent or fail
with an unsupported-option/platform diagnostic.

## Network Policy

Development mode is friendly:

- loopback connects and listens are allowed by default;
- external connects and listens are visible in Plan/doctor/audit output when statically
  visible;
- dynamic host or port values are reported honestly as partial/dynamic metadata.

Strict mode is enforceable:

- external connects require explicit allow rules;
- external listens require explicit allow rules;
- loopback/local defaults may remain easy for developer workflows unless strict config
  chooses to require explicit loopback grants;
- denied operations fail before native socket admission;
- audit output redacts sensitive endpoint details when policy requires it.

Policy checks are not an OS sandbox claim. They are Sloppy runtime admission checks and
doctor/audit evidence.

## Feature and Plan Model

Feature id: `stdlib.net`.

Public import: `sloppy/net`.

Private V8 intrinsic namespace: `__sloppy.net`.

Dependencies: `core`, `v8`, `transport.libuv`, and `stdlib.time`.

Compiler behavior in this PR:

- named `sloppy/net` imports add `stdlib.net` to Plan `requiredFeatures[]`;
- `features.network = true`;
- `strongPlan.evidence.network = true`.

Future compiler/doctor behavior:

- statically visible literal connect targets emit network capability metadata;
- statically visible literal listen targets emit network capability metadata;
- dynamic host/port values emit partial/dynamic metadata, not guessed endpoints;
- source locations point to the API call or nearest literal option object where available.

Runtime behavior after CORE-NET-01.C/D/H:

- `stdlib.net` is known to the feature registry;
- default availability is true when the runtime lane has V8, libuv transport, and
  `stdlib.time`;
- native `TcpClient` connections use a Slop-owned C API backed by private libuv TCP
  handles;
- `TcpConnection` supports bounded write, `writeText`, `read`, `readUntil`, `readLine`,
  endpoint metadata, close, abort, stale/closed-handle diagnostics, and embedded-NUL byte
  round trips;
- the V8 bridge exposes only JS-safe resource IDs and settles Promises on the owner thread
  after native worker-thread completion.

## Diagnostics

Stable network diagnostics:

- `SLOPPY_E_NET_FEATURE_UNAVAILABLE`
- `SLOPPY_E_NET_CONNECT_DENIED`
- `SLOPPY_E_NET_LISTEN_DENIED`
- `SLOPPY_E_NET_INVALID_HOST`
- `SLOPPY_E_NET_INVALID_PORT`
- `SLOPPY_E_NET_DNS_FAILURE`
- `SLOPPY_E_NET_CONNECT_TIMEOUT`
- `SLOPPY_E_NET_CONNECT_CANCELLED`
- `SLOPPY_E_NET_CONNECTION_CLOSED`
- `SLOPPY_E_NET_STALE_HANDLE`
- `SLOPPY_E_NET_READ_WRITE_TIMEOUT`
- `SLOPPY_E_NET_READ_WRITE_CANCELLED`
- `SLOPPY_E_NET_BACKPRESSURE_OVERFLOW`
- `SLOPPY_E_NET_UNSUPPORTED_OPTION`
- `SLOPPY_E_NET_BACKEND_UNAVAILABLE`

Diagnostics may name operation, policy mode, loopback/external classification, supported
option, and redacted endpoint shape. They must not include secrets, headers, tokens,
raw sockets, libuv handles, OS handles, V8 handles, raw native pointers, or package-manager
state.

## Implementation Boundaries

Native TCP implementation must stay behind Sloppy-owned C interfaces. libuv types remain
private under platform backends. Public headers and JS APIs do not expose libuv, WinSock,
epoll, kqueue, io_uring, OS handles, sockets, or raw native pointers.

DNS/connect work must not block the V8 owner thread. Cross-domain data is copied/owned
before crossing domains. Promise settlement happens on the V8 owner thread. Cleanup is
exactly once across success, failure, timeout, cancellation, abort, and shutdown.

## Evidence Boundaries

Default tests use deterministic localhost/loopback behavior only once the backend lands.
External live-network tests are optional and reported separately. Skipped live-network
lanes are not pass evidence.

Evidence lanes remain separate:

- default non-V8 lane;
- V8-gated lane;
- package lane;
- live-network/live-provider lane;
- stress/torture lane;
- benchmark lane.

CORE-NET-01 does not add CORE-NET-02 local IPC, Unix domain sockets, Windows named pipes,
TLS, HTTP client, UDP, WebSocket, Node/Bun/Deno compatibility, direct WinSock/epoll/kqueue/
io_uring backends, crypto implementation, package-manager behavior, public alpha docs, or
benchmark/performance claims.
