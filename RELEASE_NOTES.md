# Release Notes

These notes are for manual artifact dry-runs. Hosted release notes are prepared
after the readiness gate accepts the validation report.

## Release Type

- Package kind: manual dry-run artifact package.
- GitHub release: not created.
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

- GitHub release publishing.
- Hosted V8 SDK artifact publication for every supported platform.
- Signing, notarization, installers, and package-manager wrappers.
- npm publish and registry metadata.
- Final product documentation.
- Final verification.

## Known Limitations

See `docs/release/KNOWN_LIMITATIONS.md`.

## Boundary Confirmation

- Dry-run output stays separate from GitHub release publishing.
- Production readiness is tracked separately.
- Benchmark and performance wording requires measured benchmark reports.
- Node, Bun, and Deno compatibility are separate design tracks.
- npm launcher packages install the runtime; app dependency support and
  `node_modules` resolution are separate work.
- V8, package, provider, and platform readiness are reported by their own lanes.
