# Known Limitations

Review this file before each artifact dry-run. Keep every item as implemented,
blocked, unavailable, deferred, or not run. Report optional lanes under their
own status.

## Platform Status

- Windows x64: first alpha npm platform package target
  `@slopware/sloppy-win32-x64`.
- Linux x64 glibc: first alpha npm platform package target
  `@slopware/sloppy-linux-x64`; release artifacts are built on a glibc
  2.31 baseline and validated across Debian-family and Fedora-family glibc
  images.
- Linux x64 musl/Alpine: no alpha npm platform package; this requires a
  separate musl build and package lane.
- macOS arm64 and macOS x64: first alpha macOS package targets
  `@slopware/sloppy-darwin-arm64` and
  `@slopware/sloppy-darwin-x64`.
- Linux arm64 and Windows arm64: no alpha npm platform package.

## V8 SDK and Runtime

- Windows x64 contributors can provision the pinned SDK with
  `tools/windows/fetch-v8.ps1` or `tools/windows/resolve-v8-sdk.ps1 -Fetch`.
- Release packaging consumes existing GitHub V8 SDK cache assets or restored
  SDK caches. It does not rebuild V8 inside the release-artifacts workflow.
- Linux and macOS maintainers can rebuild SDK cache assets only through the
  separate V8 SDK producer workflow or local maintainer tooling.
- Default package smoke checks layout and basic CLI behavior.
- V8 SDK headers and import libraries are not bundled in default packages.
- V8 runtime support is included only when the package was explicitly built
  with a matching Sloppy-owned SDK and a V8-enabled smoke lane records evidence.

## Package and Release Limits

- Packages are public alpha artifacts.
- No installer, signing, notarization, auto-update, Homebrew, winget, or public
  GitHub release is included in the dry-run.
- npm dry-run packages install Sloppy itself; app dependencies are still
  project build inputs that must be bundled into Sloppy artifacts. The runtime
  does not discover `node_modules` at package run time.
- Package smoke validates outside-checkout layout and basic CLI behavior only.
- Live provider behavior needs separate opt-in validation.

## Deferred Release Work

- More formal V8 SDK cache retention policy.
- Signing, notarization, installers, and package-manager distribution.
- Final hosted release notes after the readiness gate accepts the validation report.
- Final production-readiness verification.
