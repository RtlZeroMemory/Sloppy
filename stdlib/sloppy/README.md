# Sloppy Bootstrap Stdlib

Status: Bootstrap app-host skeleton.

This directory is the source-controlled bootstrap standard library layout for the future
public Sloppy TypeScript API facade.

Source layout:

```text
stdlib/sloppy/
  index.js
  codec.js
  crypto.js
  data.js
  fs.js
  net.js
  time.js
  results.js
  schema.js
  app.js
  internal/
    intrinsics.js
    runtime-classic.js
  bootstrap.manifest.json
  README.md
```

Build and install layout:

```text
lib/sloppy/bootstrap/sloppy/
```

The current modules implement the first app-host foundation and developer ergonomics
facade. `index.js` re-exports frozen `Sloppy`, `data`, `sql`, filesystem API namespaces,
Codec API namespaces, `Results`, and `schema` objects. The implemented `Results.*`
helpers return plain frozen descriptor objects.
`time.js` publishes the CORE-TIME-01 API names and stable error classes. In V8-enabled
runtimes whose active Plan enables `stdlib.time`, it uses the private `__sloppy.time`
bridge for native delay and monotonic deadline accounting; otherwise runtime scheduling
fails closed with the existing missing-feature error.
`schema` exposes a small validation skeleton for string, number, boolean, and object
shapes.
`codec.js` publishes the CORE-CODEC-01.C/D/I Base64, Base64Url, Hex, and UTF-8 text
helpers plus the streaming UTF-8 decoder, and CORE-CODEC-01.E Binary reader/writer
helpers. The Base64/Base64Url/Hex decoders are strict and diagnostic-stable, arbitrary
bytes and embedded NUL values are preserved, UTF-8 malformed input follows the documented
fatal/replacement policy, and Binary reads/writes are endian-explicit and bounds-checked.
CORE-CODEC-01.F/G adds `Compression.gzip`, `Compression.gunzip`, `gzipStream`, and
`gunzipStream`. These helpers require the active V8 `__sloppy.codec` bridge, use the
selected zlib backend for gzip/gunzip bytes, keep input/output bounded, and expose an
async-iterable transform surface rather than Web Streams compatibility. CORE-CODEC-01.H/J
adds `Checksums.crc32` as a deterministic non-security checksum helper.
`File`, `Directory`, `Path`, `FileHandle`, and `FileWatcher` expose the CORE-FS-01.G filesystem
surface when the V8 runtime installs the feature-gated `__sloppy.fs` bridge: async core
file operations, directory create/list/delete/walk helpers, atomic writes, temp paths,
symlink/readlink entry points, chunked FileHandle reads/writes, and bounded non-recursive
file/directory watch events. Watch is not a Node `fs.watch` compatibility layer and does
not claim recursive or OS-native coalescing semantics.
`net.js` publishes the CORE-NET-01 TCP client API surface. In V8-enabled runtimes whose
active Plan enables `stdlib.net`, `TcpClient.connect(...)` and `TcpConnection` operations
use the private `__sloppy.net` bridge. The current runtime slice covers TCP client
connections, bounded byte/text writes, `read`, `readUntil`, `readLine`, close, and abort.
`TcpListener.listen` now uses the runtime `__sloppy.net` bridge for loopback/numeric TCP
listeners, async accept iteration, and close/abort. DNS, external-network policy
enforcement, and final socket-option coverage remain later CORE-NET slices.
`sql` and `data` expose bootstrap query-template lowering, the fake-provider contract, and
SQLite/PostgreSQL/SQL Server provider metadata. `data.sqlite("main")` and
`data.sqlite.open(...)` return safe SQLite wrappers only when the V8 runtime installs the
native SQLite bridge and passes Plan/capability metadata into the engine; in bootstrap-only
or non-V8 contexts they report bridge-unavailable. In V8 contexts with that bridge
installed, denied SQLite operations fail with capability-access errors rather than
bridge-unavailable errors. `data.postgres.open(...)` and
`data.sqlserver.open(...)` are future stdlib entry points for native providers and still
report bridge-unavailable until their own bridge modules exist.
`Sloppy.createBuilder()` exposes minimal typed config, logging, capabilities, and services builders;
`Sloppy.module(...)` creates bootstrap app module definitions; `builder.addModule(...)`
registers them; `builder.build()` freezes builder mutation and returns an in-memory app
object. Config supports case-insensitive logical keys, nested object flattening, typed
getters, and `bind(prefix, schema)`. `Sloppy.create()` remains supported as a default
builder plus `build()`.

The current app object supports `app.mapGet(...)`, `app.mapGroup(...)`, route registration
storage, route metadata storage, `.withName(...)`, structural `app.freeze()`,
`app.isFrozen()`, `app.config`, `app.log`, `app.capabilities`, `app.services`,
`app.__getRoutes()`, and
`app.__debug().modules` for bootstrap tests/debugging.

`internal/runtime-classic.js` is the EPIC-24 V8-gated bridge asset for generated artifacts.
It is not the public ESM stdlib and it is not a Node compatibility shim. `sloppy run`
loads this classic script from the staged bootstrap stdlib root before evaluating generated
`app.js`; generated code reads `globalThis.__sloppy_runtime.Results` and other lowered
stdlib exports such as `Base64`/`Text`, then registers handlers through the V8-installed
`__sloppy_register_handler(id, handler)` intrinsic.

Not implemented here:

- `app.run`, `app.listen`, or `app.build`;
- public handler registration APIs;
- compiler extraction or `app.plan.json` emission;
- HTTP server behavior or response writing;
- JavaScript-to-native PostgreSQL/SQL Server provider calls;
- database connections or SQL execution from JavaScript outside the V8-gated,
  capability-wired SQLite bridge;
- nested route groups, middleware, automatic validation/request binding, module package
  loading, or native plugins;
- config file/environment/CLI loading inside the JS stdlib itself, or secret managers;
- console, file, or native logging sinks;
- request-scoped service lifetimes, disposal hooks, async factories, or typed tokens;
- broad runtime intrinsic binding;
- Node/npm module resolution, compiler module extraction, or arbitrary import rewriting.
## ENGINE-14 Import Surface

ENGINE-14 adds a source-level compiler contract for `"sloppy"` and
`"sloppy/providers/sqlite"` imports. This directory is still the staged bootstrap stdlib,
not a Node compatibility shim, and `sloppy run` still loads the classic runtime script
before evaluating generated artifacts.
