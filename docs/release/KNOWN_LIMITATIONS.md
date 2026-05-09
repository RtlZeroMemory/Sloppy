# Known Limitations

Review this file before each artifact dry-run. Keep every item as implemented,
blocked, unavailable, deferred, or not run. Do not convert skipped optional
lanes into pass evidence.

## Platform Status

- Windows x64: supported for the current local developer lane when the Windows
  gates pass.
- Linux x64: experimental hosted/source-build lane; package dry-run evidence is
  separate.
- macOS arm64: experimental hosted/source-build lane; package dry-run evidence
  is separate.
- macOS x64: optional hosted dry-run lane when the Intel runner is requested.
- Windows arm64: no package or V8 evidence is currently claimed.

## V8 SDK and Runtime

- Windows x64 contributors can provision the pinned SDK with
  `tools/windows/fetch-v8.ps1` or `tools/windows/resolve-v8-sdk.ps1 -Fetch`.
- Linux x64 contributors can build the pinned Sloppy-owned SDK with
  `tools/unix/build-v8.sh`; hosted Linux SDK artifact URL, checksum, and
  retention remain separate release evidence.
- macOS V8 SDK artifacts are not current pass evidence.
- Default package smoke does not prove V8 execution.
- V8 SDK headers and import libraries are not bundled in default packages.
- V8 runtime support is included only when the package was explicitly built
  with a matching Sloppy-owned SDK and a V8-enabled smoke lane records evidence.

## Package and Release Limits

- Packages are experimental development artifacts.
- No installer, signing, notarization, auto-update, Homebrew, winget, or public
  GitHub release is included in the dry-run.
- npm dry-run packages install Sloppy itself and do not add npm app dependency
  support, `node_modules` resolution, or Node compatibility.
- Package smoke validates outside-checkout layout and basic CLI behavior; it is
  not production readiness evidence.
- Live provider behavior requires separate opt-in evidence.

## Deferred Release Work

- Hosted V8 SDK artifact upload/checksum/retention policy for non-Windows
  platforms.
- Signing, notarization, installers, and package-manager distribution.
- Final public release notes after the readiness gate accepts the evidence.
- Final verification and product-mode cutover.
