# CORE-OS-01 Issue Index

Status: CORE-OS-01.A/B source-of-truth index.

Parent EPIC: #611 CORE-OS-01 OS, Environment, Process, and Signals Runtime API.

| Scope | Issues | Status |
| --- | --- | --- |
| CORE-OS-01.A/B | #612, #613 | API contract, host policy, `stdlib.os` feature descriptor, compiler Plan activation, capability model, and diagnostic skeleton. |
| CORE-OS-01.C/H partial | #614, #619 | Implemented: System/Environment runtime API plus V8/stdlib surface. Process and Signals exports remain deferred stubs. |
| CORE-OS-01.D | #615 | Deferred: `Process.run` convenience API. |
| CORE-OS-01.E/F | #616, #617 | Deferred: `Process.start`, ProcessHandle, streaming pipes, deadlines, cancellation, shutdown, and kill semantics. |
| CORE-OS-01.G | #618 | Deferred: Signals and app lifecycle integration. |
| CORE-OS-01.I | #620 | Deferred: doctor/audit, conformance, examples, docs, and goldens. |

Current identifiers:

- Public import: `sloppy/os`.
- Runtime feature id: `stdlib.os`.
- Private V8 namespace: `__sloppy.os`.
- Capability categories: `os.info`, `env.read`, `env.list`, `process.run`,
  `process.shell`, `process.signal`, `process.kill`, and `signals.shutdown`.

`stdlib.os` is available for System and Environment runtime use. Process execution and
Signals are not implemented by that availability bit; their public methods fail closed until
the later CORE-OS-01 process/signal slices land.
