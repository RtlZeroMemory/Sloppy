# Release Artifacts

Release artifact work is dry-run package validation unless a scoped release
task changes the contract.

Important files:

- `tools/windows/package.ps1`
- `tools/windows/test-package.ps1`
- `tools/windows/release-dry-run.ps1`
- `tools/unix/package.sh`
- `tools/unix/test-package.sh`
- `.github/workflows/release-artifacts.yml`
- `docs/release/artifact-contract.md`
- `docs/release/artifact-contract.json`

Local checks:

```powershell
.\tools\windows\check-release-artifacts.ps1 -SelfTest
.\tools\windows\check-release-artifacts.ps1
.\tools\windows\dev.ps1 package
.\tools\windows\dev.ps1 test-package
```

Workflow reminders:

- package checks validate package behavior only;
- dry-run workflow is manual (`workflow_dispatch`) and read-only;
- checksum and archive verification belong to the release workflow;
- package testing extracts and validates the archive outside the checkout.
