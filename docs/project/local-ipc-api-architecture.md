# Local IPC API Architecture

Status: CORE-NET-02.A/B/F source of truth. This document defines the first local IPC API
contract, platform policy, diagnostics, and filesystem path policy for `sloppy/net`.
Backends, streams, doctor/audit output, examples, and conformance evidence land in later
CORE-NET-02 slices.

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
a separate runtime feature. The private V8 bridge remains `__sloppy.net`; future local IPC
backend functions must be registered only for active `stdlib.net` Plans.

`LocalEndpoint` is the portable API. `UnixSocket` and `NamedPipe` are explicit
platform-specific aliases that select the intended backend and must fail honestly when used
on unsupported platforms.

## API Contract

```ts
const conn = await LocalEndpoint.connect({
  path: "runtime:/my-app.sock",
  timeoutMs: 500
});

const server = await LocalEndpoint.listen({
  path: "runtime:/my-app.sock",
  unlinkExisting: true,
  permissions: "0600",
  backlog: 128
});

for await (const conn of server.acceptLoop({ signal })) {
  await handle(conn);
}
```

`connect` accepts:

- `path`: required named-root endpoint path;
- `timeoutMs`, `deadline`, and `signal`: Time-shaped options as backend slices land.

`listen` accepts:

- `path`: required named-root endpoint path;
- `unlinkExisting`: POSIX stale socket cleanup request, default false;
- `permissions`: POSIX octal mode string such as `"0600"`;
- `backlog`: bounded pending-accept backlog where the backend supports it;
- Time-shaped options as backend slices land.

The JavaScript surface currently validates the API shape and fails with
`SLOPPY_E_NET_LOCAL_IPC_FEATURE_UNAVAILABLE` when no local backend bridge is active. It does
not fake backend success.

## Path And Filesystem Policy

Local endpoint paths use filesystem named roots. `runtime:/my-app.sock` is the recommended
shape for app-owned runtime IPC. Absolute paths, drive-root paths, UNC paths, backslashes,
empty roots, and `..` traversal are rejected before backend admission.

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
- Windows named pipes must document security descriptor behavior in the Windows backend PR
  before claiming an equivalent to POSIX modes.

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

Cross-thread data is copied or owned before crossing domains. Promise settlement happens
on the V8 owner thread. Cleanup is exactly once across success, failure, cancellation,
timeout, disposal, shutdown, abort, and late completion. Late completion is cleanup-only.

## Evidence Boundaries

This PR1 policy slice covers contract, option validation, diagnostics, and docs. It is not
backend evidence and must not be reported as Unix socket or named pipe execution success.

Deferred to later CORE-NET-02 PRs:

- POSIX Unix socket backend;
- Windows named pipe backend;
- V8 bridge resource integration for executable local connections/servers;
- streams, backpressure, deadline, and cancellation hardening;
- doctor/audit goldens;
- source examples and conformance indexes;
- platform-gated integration tests.
