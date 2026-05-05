# Filesystem API Architecture

Status: CORE-FS-01.A/B/C/D/H source of truth. This document defines the intended
first `sloppy/fs` platform API contract and policy model. CORE-FS-01.C/D/H adds
the native path resolver, platform backend contract, offloaded core file operations,
and initial V8/stdlib bridge. Streams, watch, advanced operations, diagnostics
goldens, examples, and conformance land in later CORE-FS-01 slices.

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
the private `__sloppy.fs` core-operation namespace only when that feature is active.

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

## Implemented Core Operations

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

Blocking file work is submitted through the Slop-owned executor/offload path. Worker
callbacks operate on owned request data and settle JavaScript promises back on the V8
owner thread.

## Deferred To Later CORE-FS-01 Slices

- Directory, metadata, symlink, temp, atomic, and locking APIs.
- FileHandle, stream, and watch resources.
- Diagnostic goldens, doctor/audit output, examples, and conformance.
