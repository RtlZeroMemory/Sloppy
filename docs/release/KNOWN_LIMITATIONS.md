# Known Limitations

Review this file before each artifact dry-run. Keep every item as implemented,
blocked, unavailable, deferred, or not run. Report optional lanes under their
own status.

## Platform Status

- Windows x64: supported for the current local developer lane when the Windows
  gates pass.
- Linux x64: experimental hosted/source-build lane; package dry-run evidence is
  separate.
- macOS arm64: experimental hosted/source-build lane; package dry-run evidence
  is separate.
- macOS x64: optional hosted dry-run lane when the Intel runner is requested.
- Windows arm64: package and V8 package status are not established yet.

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

- Packages are experimental development artifacts.
- No installer, signing, notarization, auto-update, Homebrew, winget, or public
  GitHub release is included in the dry-run.
- npm dry-run packages install Sloppy itself; they do not add app-level npm
  dependency support or `node_modules` resolution.
- Package smoke validates outside-checkout layout and basic CLI behavior only.
- Live provider behavior needs separate opt-in validation.

## Deferred Release Work

- Hosted V8 SDK artifact upload/checksum/retention policy for non-Windows
  platforms.
- Signing, notarization, installers, and package-manager distribution.
- Final hosted release notes after the readiness gate accepts the validation report.
- Final verification and product-mode cutover.
