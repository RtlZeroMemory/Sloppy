# Release Notes

These notes are for the `0.1.0-alpha.2` prerelease. Sloppy is still
pre-production software; use the alpha channel for experiments, demos, and
feedback.

## Release Type

- Package kind: npm alpha packages generated from release artifacts.
- npm dist-tag: `alpha`.
- GitHub release: prerelease.
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

- Web Mode with V8-backed handler execution where the package lane includes a
  V8-enabled runtime.
- Program Mode entrypoint execution.
- Package/dependency loader foundation for compatible installed JavaScript
  packages that are bundled into Sloppy artifacts.
- Partial Node compatibility shims; Sloppy is not a full Node runtime.
- FFI foundation; FFI remains unsafe and experimental.
- Benchmark/perf improvements reported as engineering measurements only, not
  superiority claims.
- Public alpha templates: `api`, `minimal-api`, `program`, `cli`,
  `package-api`, and `node-compat`.
- npm install/package flow through `@rtlzeromemory/sloppy@alpha` and platform
  packages for Windows x64, Linux x64 GNU, macOS arm64, and macOS x64.
- Docs site and release docs for the alpha package train.
- Experimental package archive layout, runtime manifest, and package checksums.
- Known limitations, license policy, and notice policy files.

## Deferred

- Hosted V8 SDK artifact publication for every supported platform.
- Signing, notarization, installers, and package-manager wrappers.
- Final product documentation.
- Final verification.

## Known Limitations

See `docs/release/KNOWN_LIMITATIONS.md`.

## Boundary Confirmation

- Dry-run output stays separate from GitHub release publishing.
- Production readiness is tracked separately.
- Benchmark and performance wording requires measured benchmark reports.
- Sloppy is not full Node compatibility, full npm ecosystem compatibility, or
  native addon/N-API compatibility.
- npm launcher packages install the runtime; app dependency support and
  `node_modules` resolution are separate work.
- V8, package, provider, and platform readiness are reported by their own lanes.
