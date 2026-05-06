# OS API Architecture

Status: CORE-OS-01.A/B source of truth. This document defines the intended first
`sloppy/os` API contract, host policy, feature metadata, capability model, diagnostics,
and evidence boundaries. It is not implementation evidence for System, Environment,
Process, Signals, process execution, streaming pipes, or platform signal handling.

## Goals

`sloppy/os` gives Sloppy apps a compact runtime-owned host-system API for:

- normalized system metadata;
- explicit environment variable reads;
- explicit argv-only process execution;
- streaming child-process handles and pipes;
- deadline, cancellation, kill, and shutdown terminal states;
- shutdown/signal lifecycle integration;
- Plan-visible authority and deterministic diagnostics.

The module must remain Slop-owned. It does not promise Node `child_process`, Deno.Command,
shell interpolation, PTY/terminal behavior, daemon supervision, package-manager behavior,
public alpha docs, benchmark claims, or raw OS/process/pipe handle access.

## Public Module

Applications import the API from:

```ts
import { System, Environment, Process, Signals } from "sloppy/os";
```

The runtime feature descriptor is `stdlib.os`. The compiler recognizes only named,
unaliased imports from `sloppy/os`; runtime value imports add `stdlib.os` to Plan
`requiredFeatures[]`, emit `features.os = true`, and set `strongPlan.evidence.os = true`.
Type-only imports do not activate the runtime feature.

CORE-OS-01.C/H makes `stdlib.os` available for the `System` and `Environment` runtime
surface. CORE-OS-01.D adds native `sl_os_process_run` and the bootstrap `Process.run`
facade. The V8 process bridge remains deferred until process work can be scheduled without
blocking the V8 owner thread. Signals remain deferred to later CORE-OS-01 slices and fail
closed through the JS facade.

## API Contract

System:

```ts
System.platform;
System.arch;
System.cpuCount;
System.tempDirectory;
System.hostname;
System.endOfLine;
```

System values are normalized Sloppy strings/numbers, not raw platform structs. Platform
backends may provide more precise values later, but unsupported values must fail or report
unknown honestly rather than guessing.

Environment:

```ts
Environment.get("MY_APP_SETTING");
Environment.has("MY_APP_SETTING");
Environment.list({ prefix: "SLOPPY_" });
```

Raw environment reads are lower-level than app configuration. Application config should
prefer CORE-CONFIG/app config. Environment diagnostics, doctor output, audit output,
examples, and goldens may show key names but never values. Secret-looking keys are
redacted deterministically.

Process:

```ts
const result = await Process.run("git", ["status"], {
  cwd: "./repo",
  timeoutMs: 5000,
  capture: "text"
});

const proc = await Process.start("ffmpeg", ["-version"], {
  stdout: "pipe",
  stderr: "pipe",
  deadline
});

await proc.stdin.writeText("input");
for await (const line of proc.stdout.readLines()) {
  console.log(line);
}
const exit = await proc.wait();
```

`Process.run(command, args, options?)` is a convenience wrapper over explicit argv only.
It has bounded stdout/stderr capture, stable result/error shape, timeout/deadline/signal
support, and no shell default.

`Process.start(command, args, options?)` returns a `ProcessHandle` with JS-safe resource
identity only. The handle may expose `stdin`, `stdout`, `stderr`, `wait`, `terminate`,
`kill`, `cancel`, and `dispose` semantics as the implementation slices land. JavaScript
must never receive raw PIDs-as-capabilities, native process handles, pipe handles, libuv
handles, OS handles, or native pointers.

Signals:

```ts
Signals.onShutdown(async (ctx) => {
  await cleanup({ signal: ctx.signal });
});
```

Shutdown handlers integrate with app lifecycle. Platform signals are normalized where
supported and reported honestly where unsupported. Handler failures are surfaced with
stable diagnostics and redacted context.

## Security And Policy

Development mode may be friendly for local workflows, but strict mode must be enforceable:

- `os.info` controls system metadata;
- `env.read` controls individual environment reads;
- `env.list` controls listing;
- `process.run` controls process execution;
- `process.shell` is reserved for a future explicit shell API or gated option and is not
  implicit;
- `process.signal` controls sending signals/termination where distinct from kill;
- `process.kill` controls forceful termination;
- `signals.shutdown` controls shutdown handler registration.

Process execution must be explicit user intent. Shell execution is absent in the initial
contract; if added later, it must be separate or heavily gated. Environment names, command
names, cwd policy names, capture modes, and redacted endpoint shapes may appear in
diagnostics. Environment values, secret args, tokens, passwords, and sensitive captured
output must not.

Capability checks are Sloppy admission checks, not OS sandboxing. Documentation and audit
output must not claim process sandbox containment.

## Runtime Boundaries

- Platform APIs stay under `src/platform/*`.
- C17 native code uses existing Slop memory, string, buffer, resource, diagnostics, time,
  cancellation, and redaction primitives.
- V8 types stay under `src/engine/v8/*`.
- libuv types remain private implementation details.
- Blocking process/env/path/platform work must not block the V8 owner thread when it can
  block materially.
- Cross-domain data must be copied or owned before crossing threads/domains.
- Promise settlement happens on the V8 owner thread.
- Cleanup runs exactly once across success, failure, cancellation, timeout, disposal,
  shutdown, kill, and late completion.
- Late completion is cleanup-only and must not double-settle.
- Timeout, cancellation, kill, start failure, pipe closure, and shutdown are distinct
  terminal states.

## Diagnostics

Stable OS diagnostics:

- `SLOPPY_E_OS_FEATURE_UNAVAILABLE`
- `SLOPPY_E_OS_ENV_ACCESS_DENIED`
- `SLOPPY_E_OS_ENV_SECRET_REDACTED`
- `SLOPPY_E_OS_PROCESS_EXECUTION_DENIED`
- `SLOPPY_E_OS_SHELL_EXECUTION_DENIED`
- `SLOPPY_E_OS_COMMAND_NOT_FOUND`
- `SLOPPY_E_OS_INVALID_CWD`
- `SLOPPY_E_OS_INVALID_ENV_OVERRIDE`
- `SLOPPY_E_OS_PROCESS_TIMEOUT`
- `SLOPPY_E_OS_PROCESS_CANCELLED`
- `SLOPPY_E_OS_PROCESS_KILLED`
- `SLOPPY_E_OS_PROCESS_START_FAILED`
- `SLOPPY_E_OS_PIPE_CLOSED`
- `SLOPPY_E_OS_UNSUPPORTED_PLATFORM_SIGNAL`
- `SLOPPY_E_OS_SIGNAL_HANDLER_FAILURE`

Diagnostics may name operation, policy mode, capability id, environment key name, command
basename, capture mode, terminal state, and platform support class. They must not include
environment values, secret args, sensitive output, raw native handles, PIDs-as-authority,
libuv handles, OS handles, V8 handles, native pointers, package-manager state, or
machine-local paths unless a path has been normalized/redacted for the diagnostic.

## Plan, Doctor, And Audit Model

Current compiler behavior records import activation only. Later slices may add static
metadata for literal environment keys, literal process commands, capture modes, shutdown
handler registration, and dynamic/partial markers. Dynamic command names, args,
environment keys, cwd values, and signal names must be represented as partial/dynamic
metadata rather than guessed.

Doctor/audit output should make OS authority visible without executing user code. It may
report declared capabilities, visible process/env/signal usage, strict-policy gaps, and
platform limitations. It must not print secrets or claim OS sandboxing.

## Evidence Boundaries

Evidence lanes stay separate:

- default non-V8 feature/diagnostic/compiler metadata;
- V8-gated stdlib/runtime bridge evidence;
- platform-specific process/env/signal evidence;
- live-process deterministic fixture evidence;
- stress/torture cleanup evidence;
- benchmark lane.

Skipped optional lanes are not pass evidence. CORE-OS-01.A/B covers only the default
feature/Plan/diagnostic/compiler metadata lane.

## Implemented In CORE-OS-01.C/H Partial

- System metadata normalization for platform, architecture, CPU count, temp directory,
  hostname, and end-of-line.
- Environment get/has/list with development and strict host-policy admission.
- Secret-key detection and deterministic redaction helper for diagnostics and future audit
  output.
- V8 private namespace `__sloppy.os` and bootstrap JS exports for `System` and
  `Environment`.

## Implemented In CORE-OS-01.D

- Native `sl_os_process_run` for explicit argv only; no shell interpolation and no Node or
  Deno compatibility surface.
- Development-mode process execution and strict-policy `process.run` denial.
- Bounded stdout/stderr capture with truncation flags.
- Timeout terminal state distinct from command failure.
- Stable diagnostics for denied execution, command lookup failure, invalid cwd, invalid
  environment overrides, start failure, and timeout.
- Bootstrap JS `Process.run` validation and result forwarding through the Slop-owned OS
  bridge shape.

## Deferred Beyond CORE-OS-01.D

- V8 `processRun` intrinsic and owner-thread-safe native scheduling.
- Process.start, streaming pipes, and ProcessHandle lifecycle.
- Deadlines, cancellation, kill, shutdown, and late-completion runtime hardening.
- Signals and app lifecycle integration.
- Doctor/audit examples, conformance, and goldens beyond diagnostic shape.
- Public alpha docs and benchmark/performance claims.
