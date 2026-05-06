# CORE-OS-01 Issue Index

Status: CORE-OS-01.A/B source-of-truth index.

Parent EPIC: #611 CORE-OS-01 OS, Environment, Process, and Signals Runtime API.

| Scope | Issues | Status |
| --- | --- | --- |
| CORE-OS-01.A/B | #612, #613 | API contract, host policy, `stdlib.os` feature descriptor, compiler Plan activation, capability model, and diagnostic skeleton. |
| CORE-OS-01.C/H partial | #614, #619 | Deferred: System/Environment runtime API and V8/stdlib surface. |
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

`stdlib.os` is known but unavailable by default until the runtime implementation slices
land. This lets Plan validation and diagnostics fail closed without implying that process
execution, environment access, or signals are implemented.
