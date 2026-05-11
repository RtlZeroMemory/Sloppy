# Known Limitations

Review this file before each artifact dry-run. Keep every item as implemented,
blocked, unavailable, deferred, or not run. Report optional lanes under their
own status.

## Platform Status

- Windows x64: public alpha npm platform package
  `@rtlzeromemory/sloppy-win32-x64`.
- Linux x64 glibc: public alpha npm platform package
  `@rtlzeromemory/sloppy-linux-x64`.
- macOS arm64 and macOS x64: deferred npm platform packages pending
  Mac-built artifact and registry install smoke evidence for
  `@rtlzeromemory/sloppy-darwin-arm64` and
  `@rtlzeromemory/sloppy-darwin-x64`.
- Linux arm64 and Windows arm64: no alpha npm platform package.

## V8 SDK and Runtime

- Windows x64 contributors can provision the pinned SDK with
  `tools/windows/fetch-v8.ps1` or `tools/windows/resolve-v8-sdk.ps1 -Fetch`.
- Linux x64 contributors can build the pinned Sloppy-owned SDK with
  `tools/unix/build-v8.sh`; hosted Linux SDK artifact URL, checksum, and
  retention remain separate release evidence.
- macOS V8 SDK artifacts are tracked separately from the default package lane.
- Default package smoke checks layout and basic CLI behavior.
- V8 SDK headers and import libraries are not bundled in default packages.
- V8 runtime support is included only when the package was explicitly built
  with a matching Sloppy-owned SDK and a V8-enabled smoke lane records evidence.

## Package and Release Limits

- Packages are public alpha, pre-production artifacts.
- No installer, signing, notarization, auto-update, Homebrew, winget, or public
  GitHub release is included in the dry-run.
- npm dry-run packages install Sloppy itself; app dependencies are still
  project build inputs that must be bundled into Sloppy artifacts. The runtime
  does not discover `node_modules` at package run time.
- Package smoke validates outside-checkout layout and basic CLI behavior only.
- Live provider behavior needs separate opt-in validation.

## Deferred Release Work

- Hosted V8 SDK artifact upload/checksum/retention policy for non-Windows
  platforms.
- Signing, notarization, installers, and package-manager distribution.
- Final hosted release notes after the readiness gate accepts the validation report.
- Final production-readiness verification.
