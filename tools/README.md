# Tools

Tooling is organized by platform.

```text
tools/
  windows/
  unix/
```

PowerShell scripts under `tools/windows/` are the most complete validated local developer
workflow today. Root `tools/*.ps1` scripts are compatibility forwarders. Unix shell scripts
belong under `tools/unix/`.

Packaging scripts create experimental local artifacts under ignored `artifacts/packages/`:

- `tools/windows/package.ps1` creates the Windows ZIP package and checksum.
- `tools/windows/test-package.ps1` extracts a ZIP outside the checkout and runs smoke
  checks.
- `tools/unix/package.sh` creates a Linux/macOS TAR package when run on those platforms.

Generated build outputs, dependency SDKs, release archives, and local binaries must not be
committed.
