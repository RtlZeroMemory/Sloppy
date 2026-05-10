# Security Policy

Sloppy is pre-alpha and is not production-ready.

## Supported Versions

Only the current alpha line is reviewed on a best-effort basis. Older alpha
packages may be incomplete and should not be used for production systems.

## Reporting A Vulnerability

Please open a private security advisory on GitHub if the issue should not be
public. Include:

- Sloppy version or commit;
- operating system and architecture;
- install method;
- minimal reproduction;
- expected impact;
- any logs or diagnostics with secrets removed.

There is no paid bounty program.

## Scope

Security reports are useful for:

- secret leakage in diagnostics, logs, generated artifacts, or docs;
- capability or provider boundary bypasses;
- unsafe filesystem, process, or network behavior;
- package install behavior that fetches or executes unexpected code;
- native memory-safety issues.

Production deployment hardening remains future work.
