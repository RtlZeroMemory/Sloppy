# Release Artifact Policy

This directory holds release artifact policy and dry-run validation inputs.
It is not public release documentation.

Release dry-runs may build packages, verify checksums, upload workflow
artifacts, and record evidence. They must not create a public GitHub release,
sign or notarize artifacts, publish package-manager metadata, or claim
production readiness.

GitHub Release archives are the canonical future distribution artifacts. npm
packages are launcher packages around tested archive contents only. They must
not build native code, fetch or build V8 during install, use `node-gyp`, or
imply that Sloppy apps can import arbitrary npm packages.

## Required Evidence

- package artifact created by the platform package script;
- `SHA256SUMS.txt` generated and verified against uploaded archives;
- outside-checkout package smoke for the package lane under test;
- platform status reported as pass, skipped, unavailable, not run, or failed;
- V8 status reported separately from default non-V8 package evidence;
- known limitations, license policy, notice policy, and release notes reviewed;
- release policy checks passed before release notes text is reused outside the repo;
- npm root/platform dry-runs reported separately from archive smoke.

## Files

- `artifact-contract.md` and `artifact-contract.json` define archive and npm
  package shape for dry-runs.
- `runtime-dependency-audit.json` records dependency packaging policy and
  incomplete legal review status.
- `install-verification-matrix.json` records lanes that must be reported in PR
  evidence.
