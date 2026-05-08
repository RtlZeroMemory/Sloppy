# Alpha Release Dry-Run Policy

This directory holds internal release skeletons and validation policy for pre-alpha
artifact dry-runs. It is not final public alpha documentation.

Release dry-runs may build packages, verify checksums, upload workflow artifacts, and
record evidence. They must not create a public GitHub release, sign or notarize artifacts,
publish package-manager metadata, or claim production readiness.

## Required Evidence

- package artifact created by the platform package script;
- `SHA256SUMS.txt` generated and verified against the uploaded archive;
- outside-checkout package smoke for the package lane under test;
- platform status reported as pass, skipped, unavailable, untested, or failed;
- V8 status reported separately from default non-V8 package evidence;
- known limitations, license policy, notice policy, and release notes skeleton present;
- no-claims scanner passed before release notes text is reused outside the repo.

## Manual Workflow

The `release-artifacts` GitHub Actions workflow is `workflow_dispatch` only. It restores
the same Rust and vcpkg caches used by CI, builds package artifacts on selected hosted
runners, verifies checksums, runs package smoke when requested, and uploads dry-run
artifacts for inspection.

The workflow has read-only repository permissions and does not require secrets.
