# Release Notes Skeleton

These notes are a pre-alpha template for manual artifact dry-runs. They are not public
alpha release notes until the readiness gate accepts the evidence.

## Release Type

- Type: manual dry-run artifact package.
- Public release: not created.
- Signing/notarization/installers: not included.

## Evidence Summary

Use `PASS`, `FAIL`, `SKIPPED with reason`, `UNAVAILABLE with reason`, or `NOT RUN with
reason`.

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

## Shipped

- Experimental package archive layout.
- Runtime manifest.
- Package checksums.
- Known limitations, license policy, and notice policy files.
- npm launcher/platform package dry-run skeletons generated from tested archive contents.

## Deferred

- Public GitHub release.
- Hosted V8 SDK artifact publication.
- Signing, notarization, installers, and package-manager wrappers.
- npm publish and public registry metadata.
- Final public alpha documentation.
- Final alpha verification.

## Known Limitations

See `docs/release/KNOWN_LIMITATIONS.md`.

## No-Claims Confirmation

- No public alpha release claim.
- No production-readiness claim.
- No benchmark or performance claim.
- No Node, Bun, or Deno compatibility claim.
- No npm app dependency support or `node_modules` resolution claim.
- No fake V8, package, provider, or platform readiness claim.
