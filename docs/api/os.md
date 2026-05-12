# OS

`sloppy/os` is the bootstrap stdlib operating-system surface. It exposes
read-only host info, environment-variable access, subprocess execution
helpers, and a single shutdown-signal hook.

## Import

```ts
import {
    System,
    Environment,
    Process,
    ProcessHandle,
    Signals,
    OsError,
} from "sloppy/os";
```

The compiler recognizes `sloppy/os` as a stdlib subpath. Compiler source input
accepts all names in the import example above; importing any of them emits the
`stdlib.os` runtime feature into the Plan.

## Current status

This public alpha, pre-production API shape is committed for current
experiments. If the `__sloppy.os` bridge is missing,
the very first import-time check throws `SLOPPY_E_OS_FEATURE_UNAVAILABLE`
rather than letting the module half-load.

`Process` is the current-process identity and subprocess execution surface.
Native handles and file descriptors are never exposed to JS.

## System

`System` is a frozen namespace of read-only getters. Each access calls the
runtime bridge.

| Property | Type | Notes |
| --- | --- | --- |
| `System.platform` | `string` | OS identifier (`"win32"`, `"linux"`, `"darwin"`, …) |
| `System.arch` | `string` | CPU architecture (`"x64"`, `"arm64"`, …) |
| `System.cpuCount` | `number` | logical CPU count |
| `System.tempDirectory` | `string` | platform temp directory path |
| `System.hostname` | `string` | machine hostname |
| `System.endOfLine` | `string` | `"\r\n"` on Windows, `"\n"` on Unix |

## Environment

`Environment` is a frozen namespace for environment-variable access.

| Method | Returns |
| --- | --- |
| `Environment.get(key)` | `string \| undefined` |
| `Environment.has(key)` | `boolean` |
| `Environment.list(options?)` | `string[]` (sorted; supports `{ prefix }`) |

`key` must be a non-empty string with no `=` or NUL byte. The list result
contains names only — read each value with `get`.

`Environment` does not redact values. If you need redaction, store secrets
through Sloppy config (`Config<"KEY">`) and Sloppy logging — not by reading
env vars directly into log lines.

## Process

`Process` exposes current-process identity plus subprocess execution:

- `Process.info()` — return pid, parent pid, executable path, cwd, and
  command-line arguments when the platform exposes them.
- `Process.run(command, args?, options?)` — spawn, wait, return output.
- `Process.start(command, args?, options?)` — spawn and return a handle for
  streaming I/O and cooperative shutdown.

### `Process.info`

```ts
const current = Process.info();
// current.pid, current.parentPid, current.executablePath, current.cwd
```

The result is frozen:

```ts
{
  pid: number;
  parentPid: number;          // 0 when unavailable
  executablePath: string;     // empty when unavailable
  cwd: string;
  args: string[];             // frozen copy
  argsAvailable: boolean;
}
```

`argsAvailable` is `true` on platforms where Sloppy can snapshot the native
command line. It may be `false` on platforms without a safe current-process
argv source, when argv exceeds Sloppy's safe capture limit, or when the runtime
cannot prove it captured the complete argv snapshot. When `argsAvailable` is
`false`, `args` is an empty frozen array rather than a partial argv snapshot.
Strict OS policy denies `Process.info()` when system information is disabled.

```ts
const result = await Process.run("git", ["status", "--short"], {
    cwd: repoPath,
    capture: "text",
    timeoutMs: 5000,
});
// result.exitCode, result.stdout, result.stderr
```

### `Process.run`

| Option | Default | Notes |
| --- | --- | --- |
| `cwd` | inherited | absolute or project-relative path; no NUL |
| `env` | inherited | object of `string -> string`; keys/values validated |
| `capture` | `"text"` | `"none" \| "text" \| "bytes"` |
| `maxStdoutBytes` | `65536` | non-negative integer |
| `maxStderrBytes` | `65536` | non-negative integer |
| `timeoutMs` | `0` | `0` means no timeout |
| `deadline` | none | `Deadline` from `sloppy/time` |
| `signal` | none | `AbortSignal`-shaped cancellation |

The result is `{ exitCode, stdout, stderr }`. With `capture: "none"`,
`stdout` and `stderr` are `null`. With `"text"` they are UTF-8 strings,
with `"bytes"` they are `Uint8Array`.

### `Process.start`

```ts
const proc = await Process.start("my-tool", ["--watch"], {
    stdout: "pipe",
    stderr: "pipe",
    deadline: Deadline.after(5000),
});

for await (const line of proc.stdout.readLines()) {
    // ...
}

const { exitCode } = await proc.wait();
```

Options:

| Option | Default | Notes |
| --- | --- | --- |
| `cwd` | inherited | |
| `env` | inherited | |
| `stdin` | `"ignore"` | `"ignore" \| "pipe"` |
| `stdout` | `"ignore"` | `"ignore" \| "pipe"` |
| `stderr` | `"ignore"` | `"ignore" \| "pipe"` |
| `deadline` | none | |
| `signal` | none | |

### `ProcessHandle`

| Member | Notes |
| --- | --- |
| `proc.stdin?: ProcessInput` | present when `stdin: "pipe"` |
| `proc.stdout?: ProcessPipe` | present when `stdout: "pipe"` |
| `proc.stderr?: ProcessPipe` | present when `stderr: "pipe"` |
| `proc.wait(options?)` | `Promise<{ exitCode }>`; respects `timeoutMs`, `deadline`, `signal` |
| `proc.terminate()` | graceful shutdown (SIGTERM on Unix, `TerminateProcess` on Windows) |
| `proc.kill()` | forceful shutdown (SIGKILL on Unix, `TerminateProcess` on Windows) |
| `proc.cancel()` | cancel any in-flight bridge call |
| `proc.dispose()` | release the handle |

`ProcessPipe` (stdout/stderr):

| Method | Returns |
| --- | --- |
| `pipe.read(maxBytes?)` | `Promise<Uint8Array>` (default `maxBytes` 65536) |
| `pipe.readText(maxBytes?)` | `Promise<string>` (UTF-8) |
| `pipe.readLines(options?)` | `AsyncGenerator<string>` (`chunkSize` default 4096) |

`ProcessInput` (stdin):

| Method | Returns |
| --- | --- |
| `input.write(value)` | `Promise<void>` (`string` or `Uint8Array`) |
| `input.writeText(text)` | `Promise<void>` |
| `input.close()` | `Promise<void>` (signals EOF) |

### Process subprocess boundaries

- Process identity is snapshot-only through `Process.info()`. There is no
  mutable `process` global, `Process.exit()`, or raw pid-as-handle API.
- No process groups, priority, CPU affinity, or resource limits.
- No `child_process`, `spawn`, `exec`, or `node:child_process` compatibility.
- No detached/inherited stdio fan-out beyond `"ignore"` and `"pipe"`.
- Native handles are never exposed to JS — `ProcessHandle` is opaque.

Program Mode is the current route-free execution surface for tools that need
`Process.run` or `Process.start`. FFI and capability-scoped OS sandboxing remain
roadmap work.

## Signals

`Signals` is a frozen namespace with a single shutdown hook.

```ts
import { Signals } from "sloppy/os";

Signals.onShutdown(async (ctx) => {
    // ctx.signal: "shutdown" or platform signal name
    // ctx.forced: boolean
    // ctx.reason: any
});
```

The handler runs when the runtime requests shutdown. Exceptions thrown from
the handler are wrapped in `OsError` with code
`SLOPPY_E_OS_SIGNAL_HANDLER_FAILURE`.

There is no general POSIX signal handler API today.

## Examples

Inspect the host:

```ts
import { System, Environment, Process } from "sloppy/os";

const host = {
    platform: System.platform,
    arch: System.arch,
    cpuCount: System.cpuCount,
    feature: Environment.get("MY_APP_FEATURE"),
    pid: Process.info().pid,
};
```

Run a child process and capture output:

```ts
import { Process } from "sloppy/os";

const result = await Process.run("git", ["rev-parse", "HEAD"], {
    cwd: ".",
    capture: "text",
    timeoutMs: 5000,
});
```

Stream stdout:

```ts
import { Process } from "sloppy/os";
import { Deadline } from "sloppy/time";

const proc = await Process.start("my-tool", ["--watch"], {
    stdout: "pipe",
    deadline: Deadline.after(5000),
});

for await (const line of proc.stdout.readLines()) {
    // ...
}

const { exitCode } = await proc.wait();
```

In-repo references:

- `examples/os-runtime-api`
- `examples/core-process-time-codec`

## Boundaries

- No `process` global. Sloppy does not provide Node's `process`,
  `process.env`, or `process.argv`.
- `node:os` and `node:process` are partial compatibility modules in dependency
  graphs; they are not a global Node process model. `node:child_process`
  remains unsupported.
- Installed package support is limited to compatible bundled JavaScript. Use
  Sloppy modules (`sloppy/*`) for OS/process APIs when possible.
- No general POSIX signal-handler API. `Signals.onShutdown` is the one hook.

## Compiler source-input support

The compiler accepts `import { System, Environment, Process, ProcessHandle, Signals, OsError } from "sloppy/os"`.
Other names are rejected. Aliased and default imports are rejected. Importing
any supported name marks the app as needing the `stdlib.os` runtime feature.

## Runtime requirements

`sloppy/os` requires the `__sloppy.os` V8 intrinsic namespace, which the
native runtime registers when the Plan declares `stdlib.os`. The bridge
provides system info, environment access, subprocess management, and
shutdown notifications. Subprocess work runs through the platform's standard
process APIs (libuv on the runtime side).

## Errors

`OsError` is the structured error class. `error.code` carries:

- `SLOPPY_E_OS_FEATURE_UNAVAILABLE` — bridge not installed.
- `SLOPPY_E_OS_PROCESS_TIMEOUT` — `Process.run` / `Process.start` /
  `ProcessHandle.wait` exceeded its deadline.
- `SLOPPY_E_OS_PROCESS_CANCELLED` — operation cancelled by signal.
- `SLOPPY_E_OS_SIGNAL_HANDLER_FAILURE` — exception in `Signals.onShutdown`
  handler.

Validation mistakes (non-string commands, bad option shapes, NUL bytes) throw
plain `TypeError`.
