# Filesystem API Architecture

Status: CORE-FS-01.A/B/C/D/E/F/G/H/I/J source of truth plus CORE-FS-02 internal file-access
integration. This document defines the intended first `sloppy/fs` platform API contract
and policy model. CORE-FS-01.C/D/H adds the native path resolver, platform backend
contract, offloaded core file operations, and initial V8/stdlib bridge. CORE-FS-01.E/F
adds advanced operations, FileHandle, and minimal filesystem streams. CORE-FS-01.G adds
resource-backed watch handles and bounded watch events. CORE-FS-01.I/J adds doctor/audit
filesystem goldens and source examples for the implemented surface. CORE-FS-02 classifies
trusted runtime/bootstrap file reads separately from app-owned `sloppy/fs` access.

## Goals

- Provide a .NET-inspired async filesystem API for Sloppy applications.
- Keep ordinary development code simple, for example
  `await File.readText("./data/users.json")`.
- Make filesystem authority Plan-visible and inspectable by doctor/audit tooling.
- Keep OS-specific filesystem work behind `src/platform/*` backends.
- Keep blocking filesystem work off the V8 owner thread.
- Avoid any Node `fs` compatibility promise or public sync application-runtime API.

## Public Module

Applications import the API from `sloppy/fs`:

```ts
import { File, Directory, Path } from "sloppy/fs";
```

The runtime feature descriptor for this import is `stdlib.fs`. The compiler emits
`requiredFeatures: ["stdlib.fs"]` when it sees the import, and the V8 bridge registers
the private `__sloppy.fs` core-operation namespace only when that feature is active. Since
CORE-TIME-01.H, runtime activation treats `stdlib.time` as a dependency of `stdlib.fs` so
filesystem `timeoutMs` and deadline wrappers can use the same Time scheduler without
requiring app authors to add a separate import only for numeric filesystem timeouts.

## API Shape

The first complete API layer owns these namespaces:

- `File`: file reads, writes, append, copy, move, delete, exists, stat, open.
- `Directory`: create, list, walk, delete, exists, watch.
- `Path`: parse, combine, normalize, classify, root resolution helpers.
- `FileHandle`: async handle operations, chunks, lines, seek, truncate, flush, sync, close.
- stream primitives: minimal async iterable filesystem reads and bounded writes.
- watch primitives: async iterable filesystem events with bounded queues.

Representative calls:

```ts
const text = await File.readText("./data/users.json");
const json = await File.readJson("data:/users.json");
await File.writeJson("data:/users.json", json, { atomic: true, indent: 2 });
await Directory.create("uploads:/avatars", { recursive: true });

const file = await File.open("data:/large.log", { access: "read" });
try {
  for await (const line of file.readLines()) {
    // application work
  }
} finally {
  await file.close();
}
```

## Path Modes

Filesystem paths are classified before policy checks:

- project-relative: `./data/file.json`;
- named-root: `data:/file.json`;
- absolute: `C:/certs/dev.pem`, `/etc/app/config`.

Project-relative paths resolve under the app root/package root. Named roots are the
recommended production shape and must block traversal outside the configured root.
Absolute paths are development-friendly but risky: development mode allows them with
warnings, while strict mode requires explicit allowance.

## Capabilities

Filesystem access is modeled as Plan-visible capability facts:

- `fs.read`;
- `fs.write`;
- `fs.append`;
- `fs.delete`;
- `fs.list`;
- `fs.metadata`;
- `fs.watch`;
- `fs.lock`.

The native Plan v1 capability kind remains `filesystem`. CORE-FS-01.A/B extends the
filesystem access vocabulary beyond the older skeleton values to include `append`,
`delete`, `list`, `metadata`, `watch`, and `lock`; `readwrite` remains a coarse grant for
development and migration paths. Later slices attach path/root metadata and source
locations to inferred access facts.

## Development And Strict Mode

Development mode:

- allows project-relative paths;
- allows configured named roots;
- allows absolute paths with deterministic warnings;
- reports risky operations through doctor/audit metadata.

Strict mode:

- must be explicitly selected through config or CLI policy, not inferred from an
  environment name alone;
- allows project-relative paths only within the app/package root according to policy;
- allows named roots only when configured and granted;
- denies absolute paths unless explicitly allowed;
- may restrict delete, watch, and lock independently.

When static proof is unavailable, strict mode must reject or require explicit metadata
instead of silently assuming access is safe.

## Compiler And Plan Metadata

The compiler-owned contract is:

- detect `sloppy/fs` imports;
- emit/derive the `stdlib.fs` feature requirement;
- record statically visible filesystem operations with source locations when implemented;
- classify static string paths as project-relative, named-root, absolute, or invalid;
- represent dynamic paths as partial/unknown filesystem metadata, not as compiler crashes.

Doctor/audit consumers should show project-relative, named-root, and absolute accesses,
capability categories, source locations, development warnings, and strict-mode failures.

## Runtime Boundaries

- No public synchronous filesystem APIs for app runtime.
- No blocking filesystem operation may run on the V8 owner thread.
- Filesystem workers must not enter V8.
- Data crossing worker/owner-thread boundaries must be copied or owned.
- Promise settlement happens on the V8 owner thread.
- No raw OS handles or native pointers are exposed to JavaScript.
- Win32 UTF-16 conversion belongs in the Win32 backend.
- POSIX path behavior belongs in POSIX backends.
- libuv types remain private implementation details.

## Internal Runtime File Access

`sloppy/fs` is the user/application filesystem API. It is Plan-visible, `stdlib.fs`
feature-gated, capability-aware, development/strict-policy-aware, and app-owned. Internal
runtime file access is a different trust boundary. The runtime must be able to load and
validate the Plan, bundle, source map, bootstrap stdlib assets, project config, diagnostic
metadata, compiler artifacts, and test fixtures before the app feature set or filesystem
capability policy can be known.

CORE-FS-02 keeps that boundary explicit:

- App-visible filesystem access: `stdlib/sloppy/fs.js`, `src/engine/v8/intrinsics_fs.cc`,
  `include/sloppy/fs.h`, `src/core/fs.c`, and `src/platform/*/fs_*` implement the public
  `sloppy/fs` path. This path is activated by `requiredFeatures: ["stdlib.fs"]` and is
  subject to filesystem path/capability policy.
- Runtime bootstrap/artifact access: `src/main.c` loads `app.plan.json`, `app.js`,
  `app.js.map`, `stdlib/sloppy/internal/runtime-classic.js`,
  `stdlib/sloppy/bootstrap.manifest.json`, source-input `sloppy.json`, compiler executable
  discovery, and bootstrap asset doctor checks as trusted runtime-owned reads. These reads
  reuse the low-level native `sl_fs_read_file` / `sl_fs_stat` backend helpers for Slop-owned
  path conversion and diagnostics, but they do not call the public V8 `sloppy/fs` bridge,
  do not require `stdlib.fs`, do not require app filesystem capabilities, and are not
  blocked by app strict filesystem policy.
- Compiler/tooling access: `compiler/src/*` uses Rust `std::fs` for source, module,
  appsettings, artifact, and fixture work. That remains compiler-owned deterministic
  tooling I/O rather than a dependency on the C runtime filesystem layer.
- Diagnostic/source-frame access: shared diagnostic rendering consumes caller-supplied
  source text, and V8 source-map remapping consumes the already-loaded `app.js.map`
  artifact bytes. Missing or malformed artifact/source-map inputs keep stable diagnostics
  and fall back without raw OS error leakage.
- Test fixture access: C, CMake, Node, and Rust tests read checked-in fixtures directly
  unless they are explicitly testing app-facing filesystem policy. Fixture I/O must not be
  cited as proof that app `sloppy/fs` strict policy was exercised.
- Package/build access: CMake, packaging scripts, bootstrap asset copy checks, and package
  smoke tests inspect source/build/package files as build-system or package evidence. They
  are separate from both app filesystem policy and runtime bootstrap authority.

Runtime-owned artifact reads may share low-level filesystem helpers only where doing so
does not create a cycle with feature activation. The current shared helpers live below the
native C runtime/platform boundary and can be called before Plan runtime feature validation.
The V8 `__sloppy.fs` namespace and JS `File`/`Directory` wrappers remain on the app-facing
side of the boundary.

## Implemented Runtime Operations

CORE-FS-01.C/D/H implements the first runtime surface:

- native API and resolver entry points in `include/sloppy/fs.h`;
- project-relative, named-root, and absolute path classification;
- project-relative and named-root traversal rejection;
- development absolute-path warnings and strict-mode absolute-path denial;
- Win32 and POSIX platform backends under `src/platform/*`;
- core file operations: read/write/append bytes and text, exists, stat, copy, move,
  and delete;
- V8 `__sloppy.fs` intrinsics registered only for active `stdlib.fs` feature sets;
- optional `SlEngineOptions.filesystem_policy` enforcement for V8 filesystem calls, with
  a development fallback policy only for low-level smoke/source-input coverage until
  app-host config plumbing lands;
- `stdlib/sloppy/fs.js` wrappers for `File.readText`, `readBytes`, `readJson`,
  `writeText`, `writeBytes`, `writeJson`, `appendText`, `appendBytes`, `exists`,
  `stat`, `copy`, `move`, and `delete`.

CORE-FS-01.E/F extends that surface with:

- directory create/list/delete/exists/walk wrappers and native recursive create/delete;
- atomic writes through same-directory temporary files and replace/move;
- temporary file/directory creation with platform-generated unpredictable names;
- symlink/readlink entry points with platform-specific support and honest failure;
- Slop-level lock-file acquisition/release for contention-safe advisory lock paths;
- native FileHandle open/read/write/seek/truncate/flush/sync/close entry points;
- JS FileHandle resource-table IDs with stale-safe close and chunked async iterable
  `readChunks` / `readLines` helpers;
- binary chunk handling that preserves embedded NUL bytes.

CORE-FS-01.G extends the surface with:

- native `SlFsWatchHandle` resources opened through `sl_fs_watch_open`;
- `File.watch(path)` for file create/modify/delete observation;
- `Directory.watch(path)` for non-recursive directory entry create/modify/delete events;
- bounded per-watch event queues with deterministic overflow events;
- resource-table-backed V8 `FileWatcher` IDs with stale-safe close;
- async iterable JS watch events through `FileWatcher`;
- explicit rejection of recursive watch requests until an OS-native recursive backend lands.

The current backend is a portable polling watch built over Slop's existing platform
stat/list filesystem boundary. It does not claim Node `fs.watch` semantics, OS-native
coalescing behavior, or recursive watch support. Directory modify detection is based on
entry kind/size changes visible through the current stat/list contract; richer mtime and
rename fidelity belongs with the later diagnostics/conformance hardening slice.

CORE-TIME-01.H integrates Time concept alignment into the JavaScript filesystem facade.
`File.*`, `Directory.*`, FileHandle operations, watch open, and `FileWatcher.nextEvent`
accept `signal`, `deadline`, and `timeoutMs` alongside their existing operation options. A
pre-cancelled signal, expired deadline, or invalid timeout is rejected before the native
filesystem bridge is touched. Once native filesystem work has been submitted, the wrapper
can terminalize the user-facing Promise with `CancelledError` or `TimeoutError`, but this
slice does not claim provider-style interruption for an already-running blocking
filesystem backend operation. Runtime artifact loading remains independent from app
filesystem policy, and filesystem behavior is not broadened beyond Time option alignment.

Blocking file work is submitted through the Slop-owned executor/offload path. Worker
callbacks operate on owned request data and settle JavaScript promises back on the V8
owner thread.

## CORE-FS-01.I/J Evidence

- Doctor emits `stdlib.fs.capabilities` and `stdlib.fs.watch` checks when filesystem
  capability metadata is present.
- Audit emits `SLOPPY_AUDIT_FILESYSTEM_POLICY_VISIBLE` to make filesystem policy visibility
  explicit without claiming an OS sandbox.
- `examples/fs-basic`, `examples/fs-roots-policy`, `examples/fs-streams`, and
  `examples/fs-watch` document the first app-facing filesystem workflows.

## Deferred Beyond CORE-FS-01

- OS-native recursive watch backends.
- Public alpha documentation and package/release claims.
