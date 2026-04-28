# Tools

Tooling is organized by platform.

```text
tools/
  windows/
  unix/
```

PowerShell scripts under `tools/windows/` are the first-class Windows developer workflow.
Root `tools/*.ps1` scripts are compatibility forwarders. Future Unix shell scripts belong
under `tools/unix/`.

Generated build outputs, dependency SDKs, release archives, and local binaries must not be
committed.
