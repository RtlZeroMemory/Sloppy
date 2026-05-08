# Alpha Release Dry-Run Policy

This directory holds internal release skeletons and validation policy for pre-alpha
artifact dry-runs. It is not final public alpha documentation.

Release dry-runs may build packages, verify checksums, upload workflow artifacts, and
record evidence. They must not create a public GitHub release, sign or notarize artifacts,
publish package-manager metadata, or claim production readiness.

GitHub Release archives are the canonical distribution artifacts. npm packages are a
convenience launcher around those tested archive contents only. They must not build native
code, fetch or build V8 during install, use `node-gyp`, or imply that Sloppy apps can import
arbitrary npm packages.

## Required Evidence

- package artifact created by the platform package script;
- `SHA256SUMS.txt` generated and verified against the uploaded archive;
- outside-checkout package smoke for the package lane under test;
- platform status reported as pass, skipped, unavailable, untested, or failed;
- V8 status reported separately from default non-V8 package evidence;
- known limitations, license policy, notice policy, and release notes skeleton present;
- no-claims scanner passed before release notes text is reused outside the repo.
- npm root/platform package dry-runs, when run, generated from tested archive contents and
  reported separately from archive smoke.

## Manual Workflow

The `release-artifacts` GitHub Actions workflow is `workflow_dispatch` only. It restores
the same Rust and vcpkg caches used by CI, builds package artifacts on selected hosted
runners, verifies checksums, runs package smoke when requested, and uploads dry-run
artifacts for inspection.

The workflow has read-only repository permissions and does not require secrets.

## Handoff Files

- `artifact-contract.md` and `artifact-contract.json` define the archive and npm package
  shape for dry-runs.
- `runtime-dependency-audit.json` records dependency packaging policy and incomplete legal
  review status.
- `install-verification-matrix.json` records the lanes that must be reported in PR evidence.
- `alpha-gate-handoff.md` records the #918 handoff conditions and blocker policy.
- `post-merge-verifier.md` is the prompt for fresh contributor and normal user verification
  after this PR merges.
