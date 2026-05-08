# Alpha Gate Handoff

This file is the release/dist handoff record for the pre-alpha gate. It is not a public
release announcement and must not be used as proof that alpha shipped.

## Gate Links

- Parent release distribution issue: RELEASE-DIST-01 / GitHub issue #909.
- Packaging and npm dry-run task: GitHub issue #910.
- Alpha gate consumer: GitHub issue #918.
- Alpha infrastructure readiness context: GitHub issue #300.
- Runtime dogfood readiness context: GitHub issue #684.

## Evidence Required Before Gate Close

- Windows x64 archive built by `tools/windows/package.ps1`.
- Windows x64 archive verified by `tools/windows/test-package.ps1` from outside the
  checkout.
- npm root and platform package dry-runs generated from the tested archive contents.
- Release dry-run workflow remains manual, dry-run-only, read-only, and non-publishing.
- `tools/windows/check-alpha-claims.ps1` passes before release notes text leaves the repo.
- Linux x64 is either verified through source build, package smoke, and external app
  execution, or the exact blocker is recorded in the PR body and follow-up issue.
- V8 runtime package evidence is reported separately from default non-V8 archive evidence.
- Runtime dependency and notice review remain incomplete until a later legal/release pass.

## Current Handoff Status

This PR may hand off an alpha packaging gate only as a dry-run artifact contract. The
handoff is acceptable when the PR body records the exact command evidence, skipped lanes,
unavailable lanes, and blockers. It is not acceptable if it converts a missing Linux, V8,
live-provider, package-manager, or legal-review lane into a supported-platform claim.

## Follow-Up Owner Prompt

After merge, use `docs/release/post-merge-verifier.md` to run the fresh contributor and
normal user trials. If any required lane fails or remains unavailable, open a follow-up PR
or issue before closing #918.
