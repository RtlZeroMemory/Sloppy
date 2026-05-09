# Platform Boundaries

## Where It Lives

- `src/platform/win32/**`
- `src/platform/posix/**`
- `src/platform/libuv/**`
- `tools/windows/check-platform-boundaries.ps1`
- `tools/unix/check-platform-boundaries.sh`

## Rules

OS APIs belong under `src/platform/*`. Core modules include Sloppy headers and
portable C library headers only. Platform scanners enforce this boundary.

## Evidence

Windows and Unix scripts run platform-boundary checks as part of hygiene and
lint lanes.
