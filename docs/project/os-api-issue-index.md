# CORE-OS-01 Issue Index

Status: CORE-OS-01.A/B source-of-truth index.

Parent EPIC: #611 CORE-OS-01 OS, Environment, Process, and Signals Runtime API.

| Scope | Issues | Status |
| --- | --- | --- |
| CORE-OS-01.A/B | #612, #613 | API contract, host policy, `stdlib.os` feature descriptor, compiler Plan activation, capability model, and diagnostic skeleton. |
| CORE-OS-01.C/H | #614, #619 | Implemented: System/Environment runtime API plus V8/stdlib surface. Stabilization adds V8 `processRun`/`processStart` scheduling through the async loop. |
| CORE-OS-01.D | #615 | Implemented: `Process.run` explicit-argv convenience API with bounded capture, timeout, strict-policy denial, command-not-found, invalid-cwd, invalid-env diagnostics, Windows PATH lookup, and V8 native bridge evidence. |
| CORE-OS-01.E/F | #616, #617 | Implemented: native `Process.start` foundation with opaque ProcessHandle, stdin/stdout/stderr pipe operations, wait timeout, terminate/kill/cancel terminal state flags, stale-pipe diagnostics, bootstrap JS handle facade, and V8 JS-safe resource IDs. |
| CORE-OS-01.G | #618 | Implemented: bootstrap and V8 `Signals.onShutdown` registration facade with normalized shutdown context, disposal forwarding, and stable handler-failure diagnostics. Native/platform signal loop integration remains deferred and must not be claimed by doctor/audit evidence. |
| CORE-OS-01.I | #620 | Implemented: Plan capability vocabulary, doctor/audit metadata goldens, source example, conformance evidence index, docs, and V8-gated OS process bridge smoke evidence. |

Current identifiers:

- Public import: `sloppy/os`.
- Runtime feature id: `stdlib.os`.
- Private V8 namespace: `__sloppy.os`.
- Capability categories: `os.info`, `env.read`, `env.list`, `process.run`,
  `process.shell`, `process.signal`, `process.kill`, and `signals.shutdown`.

`stdlib.os` is available for System, Environment, `Process.run`, `Process.start`, and
`Signals.onShutdown` facade registration. Native C `sl_os_process_run` and
`sl_os_process_start` are implemented for deterministic explicit-argv process execution.
CORE-OS-01.I adds `examples.os.api_shape`, `sloppy.cli.doctor_os_*`,
`sloppy.cli.audit_os_*`, and `tests/golden/plan/valid-os-capability-accesses.plan.json`.
The V8 process bridge now uses owner-thread-safe async settlement for explicit-argv
run/start and JS-safe resource IDs for process handles. Native platform signal loop
dispatch remains deferred until it can be integrated with app lifecycle without
overclaiming platform support.
