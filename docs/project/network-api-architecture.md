# Network API Architecture

Status: CORE-NET-01.I conformance/examples/docs evidence plus CORE-NET-02.A/B/F local IPC
policy split. This document defines the low-level TCP/IP runtime API, policy model, feature
metadata, diagnostics, and first native TCP client/connection/listener implementation. The
local IPC source of truth is `docs/project/local-ipc-api-architecture.md`. This document is
not execution evidence for live external network access, TLS, HTTP client behavior, UDP,
WebSocket, Unix domain socket execution, or Windows named pipe execution.

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
Node `net` compatibility, and package-manager behavior are outside CORE-NET-01. CORE-NET-02
adds local IPC policy and API shape under the same `sloppy/net` feature without broadening
TCP behavior.

## Public Module

The public import is:

```ts
import { TcpClient, TcpListener, TcpConnection, NetworkAddress } from "sloppy/net";
```

The compiler recognizes only named, unaliased imports from `sloppy/net`. The import adds
`stdlib.net` to emitted Plan `requiredFeatures[]`, emits `features.network = true`, and
sets `strongPlan.evidence.network = true`. CORE-NET-01.G keeps `stdlib.net`
available when its dependencies are available and installs the initial `__sloppy.net`
client/connection/listener bridge for active V8 plans.

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

`NetworkAddress` parses and formats object addresses, `host:port` text, and bracketed
IPv6 `[host]:port` text without exposing sockets, OS handles, libuv handles, raw native
pointers, or resource internals to JavaScript. Unbracketed IPv6 text is rejected as
ambiguous.

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

Runtime behavior after CORE-NET-01.I:

- `stdlib.net` is known to the feature registry;
- default availability is true when the runtime lane has V8, libuv transport, and
  `stdlib.time`;
- native `TcpClient` connections use a Slop-owned C API backed by private libuv TCP
  handles;
- `TcpConnection` supports bounded write, `writeText`, `read`, `readUntil`, `readLine`,
  endpoint metadata, close, abort, stale/closed-handle diagnostics, and embedded-NUL byte
  round trips;
- `TcpListener` supports loopback listen, hostname/DNS-backed listen, ephemeral ports,
  bounded backlog,
  blocking native accept, JS async accept iteration, close, abort, stale-listener
  diagnostics, and accept timeout status mapping. Signal/deadline cancellation remains
  follow-up hardening for the final stream/cancellation slice;
- native address handling accepts numeric IPv4/IPv6 addresses and platform DNS results;
  DNS resolution happens inside the platform backend and the V8 path runs it on the native
  worker thread, not on the V8 owner thread;
- `noDelay` and `keepAlive` are applied through the backend and failures surface
  `SLOPPY_E_NET_UNSUPPORTED_OPTION`;
- the V8 bridge exposes only JS-safe resource IDs and settles Promises on the owner thread
  after native worker-thread completion.

## Doctor, Audit, Examples, And Conformance

Final CORE-NET-01 evidence is intentionally split by lane:

- `sloppy.cli.doctor_network_text`, `sloppy.cli.doctor_network_json`,
  `sloppy.cli.audit_network_text`, and `sloppy.cli.audit_network_json` prove
  Plan-visible `network` capabilities for `connect`, `listen`, and `connect-listen`.
  The audit note is `SLOPPY_AUDIT_NETWORK_POLICY_VISIBLE` and explicitly avoids an OS
  sandbox or external live-network claim.
- `examples.net.api_shape` checks `examples/net-tcp-client`, `examples/net-tcp-server`,
  `examples/net-tcp-echo`, `examples/net-policy-strict`, and
  `examples/net-deadline-cancel` for the supported source API shape and boundary text.
- `tests/conformance/net/README.md` is the conformance index for default loopback,
  diagnostics, CLI metadata, source examples, bootstrap stdlib, compiler/tooling, and
  optional V8 lanes.

Static compiler extraction of literal connect/listen targets remains a future
Plan-lowering improvement. Until then, CLI doctor/audit consume explicit Plan capability
metadata and examples document the literal source shape without guessing dynamic targets.

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

Default tests use deterministic localhost/loopback behavior, including numeric addresses
and `localhost` DNS where it resolves locally. External live-network tests are optional
and reported separately. Skipped live-network lanes are not pass evidence.

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

CORE-NET-02.A/B/F now specifies the local IPC contract and path policy separately. That
policy slice does not prove Unix socket or named pipe execution; backend evidence remains
deferred to platform-gated CORE-NET-02 implementation PRs.
