# Filesystem

`sloppy/fs` is the bootstrap stdlib filesystem surface. It exposes file and
directory primitives, an open-handle class for streaming I/O, and a watcher
class for change events. Every operation is async and goes through the
runtime's `__sloppy.fs` V8 intrinsic bridge.

## Import

```ts
import { File, Directory, FileHandle, FileWatcher, Path } from "sloppy/fs";
```

The compiler recognizes `sloppy/fs` as a stdlib subpath. Importing any of these
names emits the `stdlib.fs` runtime feature into the generated Plan and marks
the app as needing the filesystem bridge.

## Current status

This public alpha, pre-production API shape is committed for current experiments. All
operations require the
`stdlib.fs` runtime feature; without it the first call rejects with
`SLOPPY_E_UNAVAILABLE_RUNTIME_FEATURE`. There is no JS-only fallback.

## File

`File` is a frozen namespace of static async methods. Every method takes a path
string and returns a `Promise`.

Path strings must be non-empty and must not contain NUL bytes. Temp-file and
temp-directory prefixes follow the same NUL-free string rule.

| Method | Returns | Notes |
| --- | --- | --- |
| `File.readText(path, options?)` | `Promise<string>` | UTF-8 decode |
| `File.readBytes(path, options?)` | `Promise<Uint8Array>` | |
| `File.readJson(path, options?)` | `Promise<unknown>` | `readText` + `JSON.parse` |
| `File.writeText(path, text, options?)` | `Promise<void>` | `options.atomic` opt-in |
| `File.writeBytes(path, bytes, options?)` | `Promise<void>` | `options.atomic` opt-in |
| `File.writeJson(path, value, options?)` | `Promise<void>` | `options.indent` 0–10, `options.atomic` opt-in |
| `File.appendText(path, text, options?)` | `Promise<void>` | |
| `File.appendBytes(path, bytes, options?)` | `Promise<void>` | |
| `File.exists(path, options?)` | `Promise<boolean>` | |
| `File.stat(path, options?)` | `Promise<Stat>` | see below |
| `File.copy(from, to, options?)` | `Promise<void>` | `options.overwrite` |
| `File.move(from, to, options?)` | `Promise<void>` | `options.overwrite` |
| `File.delete(path, options?)` | `Promise<void>` | files only |
| `File.open(path, options?)` | `Promise<FileHandle>` | `access`, `create` |
| `File.watch(path, options?)` | `Promise<FileWatcher>` | per-file watch |
| `File.createSymlink(target, link, options?)` | `Promise<void>` | `options.directory` |
| `File.readLink(path, options?)` | `Promise<string>` | |
| `File.createTemp(directory, options?)` | `Promise<string>` | returns full path |

`File.stat()` resolves to a frozen object:

```ts
{
  exists: boolean;
  kind: "file" | "directory" | "other";
  size: number;
  modified_nsec: number;   // nanoseconds since epoch
}
```

## Directory

`Directory` is the matching frozen namespace for directory work.

| Method | Returns | Notes |
| --- | --- | --- |
| `Directory.create(path, options?)` | `Promise<void>` | `options.recursive` |
| `Directory.list(path, options?)` | `Promise<DirectoryEntry[]>` | shallow |
| `Directory.walk(path, options?)` | `AsyncGenerator<DirectoryEntry>` | recursive; `followSymlinks` opt-in |
| `Directory.delete(path, options?)` | `Promise<void>` | `options.recursive` |
| `Directory.exists(path, options?)` | `Promise<boolean>` | |
| `Directory.createTemp(parent, options?)` | `Promise<string>` | returns full path |
| `Directory.watch(path, options?)` | `Promise<FileWatcher>` | `recursive`, `queueCapacity`, `snapshotCapacity` |

`DirectoryEntry`:

```ts
{
  name: string;            // relative to the listed/walked root
  kind: "file" | "directory" | "other";
  size: number;
  modified_nsec: number;
}
```

`Directory.walk(...)` yields entries with `name` as a forward-slash relative
path (`"sub/dir/leaf.txt"`).

## FileHandle

`File.open(path, { access, create })` resolves to a `FileHandle`. `access` is
`"read"` (default), `"write"`, `"readwrite"`, or `"append"`. `create` defaults
to `true` for write-capable modes and `false` for `"read"`.

| Method | Returns | Notes |
| --- | --- | --- |
| `handle.readBytes(maxBytes?, options?)` | `Promise<Uint8Array>` | `maxBytes` 1..1048576, default 65536; zero-length on EOF |
| `handle.readText(maxBytes?, options?)` | `Promise<string>` | UTF-8 decode |
| `handle.writeBytes(bytes, options?)` | `Promise<void>` | |
| `handle.writeText(text, options?)` | `Promise<void>` | |
| `handle.seek(offset, origin?, options?)` | `Promise<number>` | `origin` `"start"\|"current"\|"end"` |
| `handle.truncate(size, options?)` | `Promise<void>` | |
| `handle.flush(options?)` | `Promise<void>` | |
| `handle.sync(options?)` | `Promise<void>` | fsync |
| `handle.close()` | `Promise<void>` | idempotent in practice; always pair with `try/finally` |
| `handle.readChunks(options?)` | `AsyncGenerator<Uint8Array>` | `chunkSize` default 65536 |
| `handle.readLines(options?)` | `AsyncGenerator<string>` | `newline` default `"\n"`, `maxLineLength` default 1048576 |

`readLines` rejects with a `SLOPPY_E_LIMIT_EXCEEDED`-coded error when a line
exceeds `maxLineLength`.

```ts
const handle = await File.open("./tmp/large.log", { access: "read" });
try {
    for await (const line of handle.readLines()) {
        // ...
    }
} finally {
    await handle.close();
}
```

## FileWatcher

`File.watch(path)` and `Directory.watch(path)` return a `FileWatcher`. The
watcher is async-iterable.

| Member | Returns | Notes |
| --- | --- | --- |
| `watcher.nextEvent(options?)` | `Promise<WatchEvent \| null>` | resolves to `null` once closed |
| `watcher.close()` | `Promise<void>` | |
| `watcher[Symbol.asyncIterator]()` | `AsyncIterator<WatchEvent>` | calls `close()` on `return` |

Watch options:

```ts
{
  recursive?: boolean;          // default false
  queueCapacity?: number;       // 1..256, default 16
  snapshotCapacity?: number;    // 1..1024, default 128 for dirs / 1 for files
  timeoutMs?: number;
  deadline?: Deadline;
  signal?: AbortSignal;
}
```

`WatchEvent`:

```ts
{
  kind: "created" | "modified" | "deleted" | "overflow";
  path: string;
  is_directory: boolean;
  overflow?: boolean;           // present on "overflow" events
}
```

## Path

`Path.classify(path)` is the only Path helper today. It returns
`"project-relative"`, `"named-root"`, `"absolute"`, or `"invalid"`. It is pure
JS — no bridge call.

| Input shape | Result |
| --- | --- |
| `"./file"`, `".\\dir\\file"` | `"project-relative"` |
| `"data:/foo"`, `"root:/foo"` | `"named-root"` |
| `"/absolute/foo"` | `"absolute"` |
| `"plain-name"` | `"invalid"` |

The compiler and runtime decide which classes are accepted in a given Plan;
classify is a structural check, not a permission check.

## Common options

Every async fs operation accepts a trailing options object with these timing
fields:

```ts
{
  timeoutMs?: number;     // 0..0xffffffff; throws InvalidDeadlineError otherwise
  deadline?: Deadline;    // from sloppy/time
  signal?: AbortSignal;   // standard or sloppy/time CancellationSignal
}
```

Race rules match `sloppy/time`: an already-aborted signal rejects immediately,
expired deadlines reject as `TimeoutError`, and operations cancelled while in
flight reject as `CancelledError`.

## Examples

Atomic JSON write:

```ts
await File.writeJson("./tmp/users.json", users, { atomic: true, indent: 2 });
```

Streaming read with deadline:

```ts
import { File } from "sloppy/fs";
import { Text } from "sloppy/codec";
import { Deadline } from "sloppy/time";

const bytes = await File.readBytes("data:/message.txt", {
    timeoutMs: 250,
    deadline: Deadline.after(500),
});
```

Watching a directory:

```ts
const watcher = await Directory.watch("./tmp", { queueCapacity: 16 });
try {
    for await (const event of watcher) {
        // event.kind, event.path, event.is_directory
    }
} finally {
    await watcher.close();
}
```

Working with examples in the repo:

- `examples/fs-basic` — read/write/delete round-trip
- `examples/fs-streams` — `FileHandle.readLines` and chunked I/O
- `examples/fs-watch` — `Directory.watch` event loop
- `examples/fs-roots-policy` — named roots vs project-relative paths
- `examples/core-fs-time-codec` — fs combined with deadlines and codec

## Boundaries

- Node `fs` compatibility is experimental and partial. It lives in explicit compatibility
  modules. `node:fs/promises` maps a practical async subset to this `sloppy/fs`
  surface; sync APIs, watchers, and full Node option parity are not provided.
- No `Buffer`. Bytes are `Uint8Array`.
- No synchronous variants. Every operation is async.
- No streaming write API beyond `FileHandle.writeBytes`/`writeText`. There is
  no `WritableStream` or chunked writer.
- No raw OS file descriptors are exposed to JS — `FileHandle` wraps an opaque
  resource id.

## Path policy

Path acceptance is enforced by the runtime, not by `Path.classify`. Whether an
absolute path or a named root resolves depends on the active Plan's filesystem
capability metadata. There is no JS-side sandboxing today — operations are
forwarded to the bridge, which rejects unresolvable paths with an error
rather than guessing. Treat that as part of the platform contract rather
than a client-enforced gate.

## Compiler source-input support

The compiler accepts `import ... from "sloppy/fs"` and emits the `stdlib.fs`
required feature plus filesystem evidence into the Plan. Aliased imports and
default imports of the module are not supported and are rejected by the
compiler before the Plan is written.

## Runtime requirements

`sloppy/fs` requires the `__sloppy.fs` V8 intrinsic namespace. The native
runtime registers it when the Plan declares `stdlib.fs`. The app-host JS test
harness can install a mock bridge for shape testing; production execution is
through `sloppy run` with V8.

## Errors

- `SLOPPY_E_UNAVAILABLE_RUNTIME_FEATURE` — bridge not installed.
- `TypeError` — argument shape failures (non-string path, non-Uint8Array bytes,
  out-of-range capacity, bad option object).
- `InvalidDeadlineError`, `TimeoutError`, `CancelledError` — from
  `sloppy/time`, raised by the timing fields.
- `SLOPPY_E_LIMIT_EXCEEDED` — `FileHandle.readLines` line over `maxLineLength`.
- Bridge-originating errors carry diagnostic messages from the C runtime via
  rejected promises.
