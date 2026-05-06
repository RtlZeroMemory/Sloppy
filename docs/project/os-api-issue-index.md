# CORE-OS-01 Issue Index

Status: CORE-OS-01.A/B source-of-truth index.

Parent EPIC: #611 CORE-OS-01 OS, Environment, Process, and Signals Runtime API.

| Scope | Issues | Status |
| --- | --- | --- |
| CORE-OS-01.A/B | #612, #613 | API contract, host policy, `stdlib.os` feature descriptor, compiler Plan activation, capability model, and diagnostic skeleton. |
| CORE-OS-01.C/H partial | #614, #619 | Implemented: System/Environment runtime API plus V8/stdlib surface. |
| CORE-OS-01.D | #615 | Implemented: `Process.run` explicit-argv convenience API with bounded capture, timeout, strict-policy denial, command-not-found, invalid-cwd, and invalid-env diagnostics. V8 native bridge remains deferred to avoid blocking the owner thread. |
| CORE-OS-01.E/F | #616, #617 | Implemented: native `Process.start` foundation with opaque ProcessHandle, stdin/stdout/stderr pipe operations, wait timeout, terminate/kill/cancel terminal state flags, stale-pipe diagnostics, and bootstrap JS handle facade. V8 owner-thread scheduling remains deferred. |
| CORE-OS-01.G | #618 | Implemented: bootstrap `Signals.onShutdown` registration facade with normalized shutdown context, disposal forwarding, and stable handler-failure diagnostics. Native/platform signal loop integration remains deferred. |
| CORE-OS-01.I | #620 | Deferred: doctor/audit, conformance, examples, docs, and goldens. |

Current identifiers:

- Public import: `sloppy/os`.
- Runtime feature id: `stdlib.os`.
- Private V8 namespace: `__sloppy.os`.
- Capability categories: `os.info`, `env.read`, `env.list`, `process.run`,
  `process.shell`, `process.signal`, `process.kill`, and `signals.shutdown`.

`stdlib.os` is available for System, Environment, the bootstrap `Process.run` facade, and
the bootstrap `Process.start` handle facade. Native C `sl_os_process_run` and
`sl_os_process_start` are implemented for deterministic explicit-argv process execution.
The bootstrap `Signals.onShutdown` facade is available through the Slop-owned bridge shape.
The V8 process bridge and native platform signal loop remain deferred until host work can
be scheduled off the owner thread.
