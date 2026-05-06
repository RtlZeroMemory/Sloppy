# Local IPC API Architecture

Status: CORE-NET-02 stabilized source of truth. This document defines the local IPC API
contract, platform policy, diagnostics, filesystem path policy, native stream lifecycle
policy, examples, and conformance evidence for `sloppy/net`.

## Goals

`LocalEndpoint` gives Sloppy apps one portable local IPC API while keeping platform-specific
truth explicit:

- Unix domain sockets on POSIX platforms where the backend supports them;
- Windows named pipes on Windows;
- no Windows AF_UNIX claim in this slice;
- no TCP/IP, TLS, UDP, WebSocket, HTTP client, or Node `net.Socket` compatibility;
- no raw socket, pipe, OS, libuv, or native resource handles exposed to JavaScript.

## Public Module

Applications import local IPC from the existing `sloppy/net` module:

```ts
import { LocalEndpoint, UnixSocket, NamedPipe } from "sloppy/net";
```

The feature id remains `stdlib.net`. Local IPC is a sub-surface of the network stdlib, not
a separate runtime feature. The private V8 bridge remains `__sloppy.net`; local IPC bridge
functions are registered only for active `stdlib.net` Plans.

`LocalEndpoint` is the portable API. `UnixSocket` and `NamedPipe` are explicit
platform-specific aliases that select the intended backend and must fail honestly when used
on unsupported platforms.

## API Contract

```ts
const conn = await LocalEndpoint.connect({
  path: "runtime:/my-app.sock"
});

const server = await LocalEndpoint.listen({
  path: "runtime:/my-app.sock",
  unlinkExisting: true,
  permissions: "0600",
  backlog: 128
});

for await (const conn of server.accept({ signal })) {
  await handle(conn);
}
```

`connect` accepts:

- `path`: required named-root endpoint path;
- `timeoutMs`, `deadline`, and `signal`: Time-shaped operation bounds. JS pre-cancel and
  expired-deadline paths fail before native admission; late native success is cleanup-only.

`listen` accepts:

- `path`: required named-root endpoint path;
- `unlinkExisting`: POSIX stale socket cleanup request, default false;
- `permissions`: POSIX octal mode string such as `"0600"`;
- `backlog`: bounded pending-accept backlog where the backend supports it;
- `timeoutMs`, `deadline`, and `signal`: Time-shaped operation bounds.

The JavaScript surface validates the API shape and fails with
`SLOPPY_E_NET_LOCAL_IPC_FEATURE_UNAVAILABLE` when no local backend bridge is active. It does
not fake backend success. `server.accept(options)` is both awaitable for one accept and
async-iterable for repeated accepts; `acceptLoop(options)` remains an alias.

The native C surface keeps compatibility wrappers for existing callers and adds explicit
I/O option forms for stream operations:

- `sl_local_connection_read_ex`;
- `sl_local_connection_write_ex`;
- `sl_local_connection_read_until_ex`;
- `sl_local_connection_read_line_ex`;
- `sl_local_connection_write_text_ex`.

`SlLocalIoOptions` carries per-operation `timeout_ms` and a caller-owned
`SlCancellationToken` snapshot. Existing non-`_ex` functions behave as unbounded blocking
operations with no cancellation token.

## Path And Filesystem Policy

Local endpoint paths use filesystem named roots. `runtime:/my-app.sock` is the recommended
shape for app-owned runtime IPC. Absolute paths, drive-root paths, UNC paths, backslashes,
empty roots, empty path segments, `.` segments, `..` traversal, and characters outside
`[A-Za-z0-9_.-]` path segments are rejected before backend admission.

The named-root resolver must be shared with the CORE-FS-01 policy model where possible:

- development mode allows configured runtime roots and project-relative development roots
  only where policy says so;
- strict mode requires an explicit local endpoint allow rule for `connect` and `listen`;
- dynamic endpoint metadata is represented as partial/dynamic, not guessed;
- runtime/bootstrap file access remains separate from app-owned local endpoint policy.

Stale socket cleanup is opt-in. `unlinkExisting: true` may remove only policy-allowed POSIX
socket files under the resolved named root. It must not remove ordinary files, directories,
symlinks, paths outside the root, Windows named pipe names, or paths denied by strict
policy. Cleanup failures use stable stale-cleanup diagnostics and leave the endpoint
unclaimed.

Permissions are honest:

- POSIX `permissions` is a requested mode applied only where the backend can enforce it;
- unsupported modes or platforms fail with `SLOPPY_E_NET_LOCAL_IPC_PERMISSION_UNSUPPORTED`;
- Windows named pipes do not claim a POSIX-mode equivalent; POSIX-style `permissions`
  fail honestly on the Windows backend.

## Platform Matrix

| API | POSIX | Windows |
| --- | --- | --- |
| `LocalEndpoint` | Uses Unix domain socket backend when available. | Uses named pipe backend when available. |
| `UnixSocket` | Supported when the POSIX backend is compiled and available. | Unsupported unless a later PR explicitly adds and tests AF_UNIX policy. |
| `NamedPipe` | Unsupported. | Supported when the Windows backend is compiled and available. |
| `permissions` | Supported for filesystem socket paths where mode changes are available. | Unsupported until a Windows security policy is specified and tested. |
| `unlinkExisting` | Stale socket cleanup only. | Not a named pipe concept; unsupported/ignored only where docs and tests say so. |

Unsupported combinations fail or skip with stable diagnostics. They are never reported as
cross-platform success.

## Plan, Capability, And Policy Metadata

`stdlib.net` remains the feature required by `sloppy/net` imports. Capability kind remains
`network`. Local IPC uses network capability access values:

- `connect`;
- `listen`;
- `connect-listen`.

Future compiler metadata should identify statically visible local endpoint paths, operation
kind, backend hint, and source location. Dynamic paths must be marked partial/dynamic.

Strict local IPC policy must deny before native backend admission when a path, backend, or
operation is not allowed. Denials use `SLOPPY_E_NET_LOCAL_IPC_PATH_DENIED` or existing
capability-denial diagnostics as appropriate.

## Diagnostics

Stable local IPC diagnostics:

- `SLOPPY_E_NET_LOCAL_IPC_FEATURE_UNAVAILABLE`;
- `SLOPPY_E_NET_LOCAL_IPC_UNSUPPORTED_PLATFORM`;
- `SLOPPY_E_NET_LOCAL_IPC_INVALID_PATH`;
- `SLOPPY_E_NET_LOCAL_IPC_PATH_DENIED`;
- `SLOPPY_E_NET_LOCAL_IPC_STALE_CLEANUP_FAILED`;
- `SLOPPY_E_NET_LOCAL_IPC_ENDPOINT_EXISTS`;
- `SLOPPY_E_NET_LOCAL_IPC_CONNECT_FAILED`;
- `SLOPPY_E_NET_LOCAL_IPC_LISTEN_FAILED`;
- `SLOPPY_E_NET_LOCAL_IPC_ACCEPT_CANCELLED`;
- `SLOPPY_E_NET_LOCAL_IPC_READ_WRITE_CANCELLED`;
- `SLOPPY_E_NET_LOCAL_IPC_DISPOSED`;
- `SLOPPY_E_NET_LOCAL_IPC_BACKEND_UNAVAILABLE`;
- `SLOPPY_E_NET_LOCAL_IPC_PERMISSION_UNSUPPORTED`.

Diagnostics may include safe operation names, backend names, named-root tokens, and
redacted endpoint shapes. They must not include raw socket paths when policy redaction
requires hiding them, raw handles, OS error internals, native pointers, V8 handles, libuv
handles, secrets, headers, tokens, or package-manager state.

## Backend Boundaries

Native local IPC implementation must stay behind Slop-owned C interfaces. POSIX socket
work belongs under `src/platform/posix/*`, Windows named pipe work under
`src/platform/win32/*`, and any libuv helper under `src/platform/libuv/*` without leaking
libuv types into public headers or JavaScript.

The native POSIX `AF_UNIX` stream backend lives behind `SlLocalConnection` and
`SlLocalServer`. The backend accepts already-resolved filesystem paths, applies opt-in
stale socket cleanup only for existing socket files, rejects ordinary files/directories as
endpoint collisions, applies POSIX mode bits where `chmod` can enforce them, and keeps file
descriptors private.

The native Windows named pipe backend lives behind the same `SlLocalConnection`
and `SlLocalServer` contract. The backend accepts explicit normalized pipe names in the
`\\.\pipe\name` namespace, rejects nested or unsafe pipe names, permits `~` only as an
internal separator for already-validated multi-segment `runtime:/` paths, keeps `HANDLE`
values private, rejects POSIX Unix sockets on Windows, rejects named-root/relative path
shapes before backend admission, and fails honestly for POSIX-style `permissions` and
`unlinkExisting`.

Native stream deadline/cancellation options exist across both backend paths.
Read/write options distinguish caller cancellation from deadline expiry by returning the
`SlCancellationToken` status code for pre-cancelled tokens and
`SL_STATUS_DEADLINE_EXCEEDED` for backend wait timeouts. Bounded read buffers and
`read_until`/`read_line` maximums remain deterministic backpressure limits and preserve
binary data, including embedded NUL bytes. POSIX connect timeouts use a nonblocking
connect wait, and Windows named pipe connect timeouts distinguish unavailable endpoints
from deadline expiry.

The validating `stdlib/sloppy/net.js` `LocalEndpoint` surface is wired to V8
bridge functions. JavaScript receives Slop-owned connection/server wrapper objects backed
only by resource IDs; Unix socket, named pipe, OS, and libuv handles remain native-private.
The bridge resolves `runtime:/...` local endpoint paths into backend-native Unix socket
paths or Windows named pipe names before calling `SlLocalConnection` and `SlLocalServer`
APIs. The runtime-path resolver rejects traversal and unsafe/colliding path spellings; it
is intentionally narrow and will be replaced by the
shared filesystem named-root resolver when that policy API is available to the V8 bridge.

Cross-thread data is copied or owned before crossing domains. Promise settlement happens
on the V8 owner thread. Cleanup is exactly once across success, failure, cancellation,
timeout, disposal, shutdown, abort, and late completion. Late completion is cleanup-only.

## Evidence Boundaries

Default evidence covers Windows named pipe native behavior, bootstrap stdlib API shape,
source examples, doctor/audit goldens, and conformance indexing. POSIX Unix domain socket
behavior is covered by POSIX-gated tests in non-Windows lanes. V8 execution is covered only
when a V8-enabled lane is configured and run; default non-V8 evidence must not be reported
as V8 success.

CORE-NET-02 does not add TCP/IP, UDP, HTTP, WebSocket, TLS, Node compatibility, public
alpha docs, package-manager behavior, benchmark evidence, or performance claims.
