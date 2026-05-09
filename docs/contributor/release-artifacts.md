# Release Artifacts

Release artifact work is dry-run and package-smoke evidence unless a scoped
release task changes the contract.

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

Policy reminders:

- package checks are lane evidence, not release-readiness evidence;
- dry-run workflow is manual (`workflow_dispatch`) and read-only;
- checksum and archive verification must stay in the release lane;
- package smoke is outside-checkout validation.
