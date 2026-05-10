# Node Compatibility Reference

Sloppy is not a Node runtime. Node compatibility is an explicit registry of
supported, partial, stubbed, and unsupported builtins that resolve to Sloppy
stdlib-backed shim modules.

The registry lets compatible pure-JavaScript packages build without giving
packages unrestricted access to Node internals.

## Status Values

| Status | Meaning |
| --- | --- |
| `supported` | The shim exposes the documented subset for that builtin and does not require a native Node API. |
| `partial` | Some useful members are implemented through Sloppy Core or pure JS; missing members throw clear errors. |
| `stubbed` | The builtin is recognized for diagnostics but does not provide useful runtime behavior yet. |
| `unsupported` | Imports fail at build time with `SLOPPYC_E_UNSUPPORTED_NODE_BUILTIN`. |

## Current Registry

| Specifier | Status | Backing module | Notes |
| --- | --- | --- | --- |
| `node:path`, `path` | `supported` | `sloppy/node/path` | POSIX-style path helpers plus `win32`/`posix` helper objects. |
| `node:events`, `events` | `supported` | `sloppy/node/events` | Basic `EventEmitter` methods. |
| `node:url`, `url` | `supported` | `sloppy/node/url` | Re-exports `URL` and `URLSearchParams` when the JS environment provides them. |
| `node:querystring`, `querystring` | `supported` | `sloppy/node/querystring` | Basic `parse` and `stringify`. |
| `node:buffer`, `buffer` | `partial` | `sloppy/node/buffer` | `Buffer.from`, `Buffer.alloc`, byte arrays, and simple encoding helpers. No full Node buffer identity. |
| `node:util`, `util` | `partial` | `sloppy/node/util` | Minimal `inspect`, `format`, `promisify`, and type helpers. |
| `node:timers`, `timers` | `partial` | `sloppy/node/timers` | Maps to available global timers where present; missing timer globals fail clearly. |
| `node:fs`, `fs` | `partial` | `sloppy/node/fs` | Maps selected async/file helpers to `sloppy/fs` where equivalent behavior exists. Watchers and many sync Node APIs are not implemented. |
| `node:fs/promises`, `fs/promises` | `partial` | `sloppy/node/fs/promises` | Promise-shaped filesystem subset backed by `sloppy/fs`. |
| `node:os`, `os` | `partial` | `sloppy/node/os` | Minimal platform/environment helpers backed by `sloppy/os` where possible. |
| `node:process`, `process` | `partial` | `sloppy/node/process` | Module import only. Sloppy does not install a global Node `process` object. |
| `node:crypto`, `crypto` | `partial` | `sloppy/node/crypto` | Random helpers where backed by Sloppy crypto; unsupported hash/HMAC members throw. |

Unsupported families include `node:stream`, `node:http`, `node:https`,
`node:http2`, `node:net`, `node:tls`, `node:dns`, `node:worker_threads`,
`node:child_process`, `node:vm`, `node:inspector`, `node:test`, `node:repl`,
and Node internal modules.

## Runtime Globals

Compatibility shims are modules, not global Node emulation. Sloppy does not
claim a global `process` or `Buffer` by default. Import the compatibility
module explicitly:

```ts
import { Buffer } from "node:buffer";
import process from "node:process";
```

Packages that require globals implicitly may still fail until a future
compatibility slice adds a documented global policy.

## Diagnostics

Unsupported builtins fail at build time:

```text
SLOPPYC_E_UNSUPPORTED_NODE_BUILTIN
node:stream is not supported by Sloppy's Node compatibility registry yet.
```

Unsupported members inside a partial shim fail at runtime with a
module-specific message, for example:

```text
node:fs.watch is not implemented by Sloppy's node:fs compatibility shim.
```

Native addons fail at build time:

```text
SLOPPYC_E_NATIVE_ADDON_UNSUPPORTED
Package "sharp" requires a native Node addon. Sloppy does not support Node native addons yet.
```

Use [`sloppy deps`](../cli/deps.md), [`sloppy audit`](../cli/audit.md), and
[`sloppy doctor`](../cli/doctor.md) to inspect registry use and compatibility
findings.

