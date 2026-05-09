# Release Notes

These notes are for manual artifact dry-runs. They are not public release notes
until the readiness gate accepts the evidence.

## Release Type

- Package kind: manual dry-run artifact package.
- Public release: not created.
- Signing/notarization/installers: not included.

## Evidence Summary

Use `PASS`, `FAIL`, `SKIPPED with reason`, `UNAVAILABLE with reason`, or
`NOT RUN with reason`.

- Windows x64:
- Linux x64:
- macOS arm64:
- macOS x64:
- V8:
- Package:
- Outside-checkout package:
- npm root/platform package dry-run:
- CI workflow:
- Optional/live-provider:

## Included Artifact Areas

- Experimental package archive layout.
- Runtime manifest.
- Package checksums.
- Known limitations, license policy, and notice policy files.
- npm launcher/platform package dry-run content generated from tested archive
  contents.

## Deferred

- Public GitHub release.
- Hosted V8 SDK artifact publication for every supported platform.
- Signing, notarization, installers, and package-manager wrappers.
- npm publish and public registry metadata.
- Final public documentation.
- Final verification.

## Known Limitations

See `docs/release/KNOWN_LIMITATIONS.md`.

## No-Claims Confirmation

- No public release claim.
- No production-readiness claim.
- No benchmark or performance claim.
- No Node, Bun, or Deno compatibility claim.
- No npm app dependency support or `node_modules` resolution claim.
- No fake V8, package, provider, or platform readiness claim.
