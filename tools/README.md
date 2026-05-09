# Tools

Tooling is organized by platform.

```text
tools/
  windows/
  unix/
```

PowerShell scripts under `tools/windows/` are the most complete validated local developer
workflow today. Root `tools/*.ps1` scripts are convenience forwarders. Unix shell scripts
belong under `tools/unix/`.

Packaging scripts create experimental local artifacts under ignored `artifacts/packages/`:

- `tools/windows/package.ps1` creates the Windows ZIP package and checksum.
- `tools/windows/test-package.ps1` extracts a ZIP outside the checkout and runs smoke
  checks.
- `tools/unix/bootstrap.sh` and `tools/unix/dev.sh` provide the Linux/macOS command
  contract.
- `tools/unix/package.sh` creates a Linux/macOS TAR package when run on those platforms.
- `tools/windows/release-dry-run.ps1` and `tools/unix/release-dry-run.sh` run manual
  artifact dry-runs and write ignored summaries under `artifacts/release-dry-run/`.
- `tools/windows/check-release-artifacts.ps1` enforces release policy and checksum
  policy.
- `tools/windows/bench.ps1` runs native benchmark smoke/list checks and the BENCH-01
  local runtime comparison engine.
- `tools/unix/bench.sh` runs the native Unix benchmark wrapper and reports the local
  runtime comparison lane as unavailable until that runner is ported.

Canonical command vocabulary:

```text
bootstrap
dev doctor
dev configure
dev build
dev test
dev lint
dev format-check
dev package
dev test-package
```

Generated build outputs, dependency SDKs, release archives, and local binaries must not be
committed.
