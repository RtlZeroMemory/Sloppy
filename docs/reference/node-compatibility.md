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
| `node:buffer`, `buffer` | `partial` | `sloppy/node/buffer` | `Buffer.from`, `Buffer.alloc`, zero-filled `allocUnsafe`, `isBuffer`, `isEncoding`, `byteLength`, `concat`, `compare`, `equals`, `slice`/`subarray`, `write`, basic unsigned integer reads/writes, and UTF-8/hex/base64/base64url conversion. No full Node buffer identity. |
| `node:console`, `console` | `partial` | `sloppy/node/console` | Reuses the Sloppy program console object where present. |
| `node:constants`, `constants` | `partial` | `sloppy/node/constants` | Small file-mode constants used by pure-JS feature detection. |
| `node:util`, `util` | `partial` | `sloppy/node/util` | Minimal `inspect`, `format`, `promisify`, `callbackify`, `inherits`, and type helpers. |
| `node:timers`, `timers` | `partial` | `sloppy/node/timers` | Maps to available global timers where present; missing timer globals fail clearly. |
| `node:fs`, `fs` | `partial` | `sloppy/node/fs` | Callback helpers and `promises` backed by `sloppy/fs`. The sync subset (`readFileSync`, `existsSync`, `statSync`) consults the sealed bundled asset map and throws `SLOPPY_E_NODE_SYNC_FS_UNSEALED` for paths outside that policy; watchers and arbitrary sync filesystem APIs are not implemented. |
| `node:fs/promises`, `fs/promises` | `partial` | `sloppy/node/fs/promises` | `readFile`, `writeFile`, `appendFile`, `copyFile`, `rename`, `stat`, `mkdir`, `readdir`, `rm`, `unlink`, `access`, `readlink`, and `symlink` where Sloppy filesystem policy allows them. `lstat`, `mkdtemp`, and `realpath` throw explicit unsupported errors until matching runtime primitives exist. |
| `node:os`, `os` | `partial` | `sloppy/node/os` | Minimal platform/environment helpers backed by `sloppy/os` where possible. |
| `node:process`, `process` | `partial` | `sloppy/node/process` | `platform`, `arch`, overlay-writable `env`, `cwd()`, `argv`, `nextTick`, Sloppy `version`/`versions`, `exitCode`, monotonic `hrtime`/`uptime`, EventEmitter-style `on`/`addListener`/`removeListener`/`emit`, `browser: false`, and minimal stdio objects. |
| `node:crypto`, `crypto` | `partial` | `sloppy/node/crypto` | `randomBytes`, `randomUUID`, SHA-2 `createHash`, SHA-256 `createHmac`, and `timingSafeEqual` backed by `sloppy/crypto`. Hash/HMAC digest helpers are Promise-shaped because the Sloppy crypto API is async. |
| `node:assert`, `assert` | `partial` | `sloppy/node/assert` | `ok`, loose `equal`, `strictEqual`, `notStrictEqual`, JSON-shaped `deepEqual`/`deepStrictEqual`, `throws`, `doesNotThrow`, `rejects`, `doesNotReject`, `fail`, `ifError`, and `AssertionError`. |
| `node:assert/strict`, `assert/strict` | `partial` | `sloppy/node/assert/strict` | Same small assert subset, with `equal` mapped to `strictEqual`. |
| `node:stream`, `stream` | `partial` | `sloppy/node/stream` | Small EventEmitter-based subset: `Readable.from`, minimal `Writable`, `PassThrough`, and `pipeline`. No full Node stream backpressure or transform contract. |
| `node:stream/promises`, `stream/promises` | `partial` | `sloppy/node/stream/promises` | Promise-shaped `pipeline` from the small `node:stream` compatibility subset. |
| `node:module`, `module` | `partial` | `sloppy/node/module` | `builtinModules` and `createRequire()` for bundled Sloppy program modules. |
| `node:string_decoder`, `string_decoder` | `partial` | `sloppy/node/string_decoder` | UTF-8 `StringDecoder` backed by Sloppy text decoding. |
| `node:zlib`, `zlib` | `partial` | `sloppy/node/zlib` | Async callback `gzip`/`gunzip` backed by Sloppy compression. `deflate`/`inflate` and sync zlib APIs throw explicit unsupported errors. |
| `node:perf_hooks`, `perf_hooks` | `partial` | `sloppy/node/perf_hooks` | Minimal `performance.now()` and `timeOrigin`. |
| `node:diagnostics_channel`, `diagnostics_channel` | `partial` | `sloppy/node/diagnostics_channel` | In-process channel publish/subscribe for package instrumentation hooks. |
| `node:tty`, `tty` | `stubbed` | `sloppy/node/tty` | `isatty()` and stream classes report non-TTY. |
| `node:http`, `http` | `stubbed` | `sloppy/node/http` | Importable client/server entry points throw `SLOPPY_E_NODE_HTTP_UNSUPPORTED`. |
| `node:https`, `https` | `stubbed` | `sloppy/node/https` | Importable client/server entry points throw `SLOPPY_E_NODE_HTTPS_UNSUPPORTED`. |

Unsupported families include `node:http2`, `node:net`, `node:tls`,
`node:dns`, `node:worker_threads`,
`node:child_process`, `node:vm`, `node:inspector`, `node:test`,
`node:repl`, and Node internal modules.

## Runtime Globals

Compatibility shims are modules first. Sloppy does not claim process-wide Node
identity. For bundled installed packages, the generated program wrapper installs
only `global`, `process`, and `Buffer` while the program entry runs, because many
pure-JS packages probe those globals. Source code should still import the
compatibility module explicitly:

```ts
import { Buffer } from "node:buffer";
import process from "node:process";
```

Packages that require other Node globals still fail unless the generated graph
has a documented shim for them.

## Diagnostics

Unsupported builtins fail at build time:

```text
SLOPPYC_E_UNSUPPORTED_NODE_BUILTIN
node:child_process is not supported by Sloppy's Node compatibility registry yet.
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

## Package Resolution Matrix

The package shapes Sloppy currently tests are committed at
`tests/fixtures/npm-compat/` with a `matrix.json` index. The compiler resolver
test walks the matrix on every test run. The matrix is the regression baseline
for currently tested package shapes; shapes outside the matrix are not
implicit non-regressions for users. Add a fixture plus matrix entry alongside
any change that broadens resolver behavior.

The resolver matrix proves package shapes resolve.

## Package Runtime Behavior Matrix

The runtime behaviors Sloppy currently tests are committed at
`tests/fixtures/npm-runtime/` with its own `matrix.json` index. Each runtime
fixture is built, packaged, copied outside the source checkout, and re-run
under V8. The matrix asserts the documented stdout for `supported` fixtures
and the documented stderr substring for `stubbed` and `negative-runtime`
fixtures.

The runtime matrix proves selected package behaviors execute after packaging.

| Group | Fixtures | Notes |
| --- | --- | --- |
| `cjs` | 3 | `module.exports = fn`, `exports.foo`/`module.exports.bar` mutation, `exports = ...` rebind no-op. |
| `interop` | 2 | CJS `require("./data.json")`, ESM app default-imports a CJS package. |
| `exports` | 2 | `import` and `require` branches of the same `exports` object, observed through ESM import and `createRequire`. |
| `self-reference` | 1 | Package imports itself by declared name; identity preserved through `createRequire`. |
| `imports` | 1 | Package `imports` `#alias` resolves at runtime. |
| `module` | 2 | `node:module` `createRequire` for sealed JSON and subpath, `require.resolve` against the sealed graph, and `__dirname`/`__filename` based on sealed module IDs. |
| `buffer` | 1 | `Buffer.from` for utf8/base64/hex, `Buffer.concat`, `Buffer.byteLength`. |
| `crypto` | 1 | `createHash("sha256")`, `randomBytes`, `timingSafeEqual`. |
| `stream` | 1 | `Readable.from` and `PassThrough` only; backpressure and Transform are out of scope. |
| `zlib` | 1 | Async callback `gzip` and `gunzip` round-trip. |
| `fs` | 1 | Sync `fs.readFileSync`/`existsSync`/`statSync` against the sealed bundled asset map (currently `stubbed` pending compile-time asset baking). |
| `negative` | 7 | Native addon rejection, unsupported `node:` builtin, computed `require` without `moduleInclude`, missing package asset, `createRequire`/`require.resolve` on unknown package, sync `fs` outside sealed policy. |

The runtime matrix and the resolver matrix are complementary; neither implies
the other.
