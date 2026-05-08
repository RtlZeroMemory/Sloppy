# Known Limitations

This template must be reviewed before each alpha artifact dry-run. Keep every item as
implemented, blocked, unavailable, or deferred. Do not convert skipped optional lanes into
pass evidence.

## Platform Status

- Windows x64: supported for the current local developer lane when the Windows gates pass.
- Linux x64: experimental hosted/source-build lane; package dry-run evidence is separate.
- macOS arm64: experimental hosted/source-build lane; package dry-run evidence is separate.
- macOS x64: optional hosted dry-run lane when the Intel runner is requested.
- Windows arm64: planned; no package or V8 evidence is currently claimed.

## V8 SDK and Runtime

- Contributors should not have to build V8 on the happy path once hosted SDK artifacts
  exist.
- Default package smoke does not prove V8 execution.
- V8 SDK headers and import libraries are not bundled in default packages.
- V8 runtime files are bundled only when the package was explicitly built with matching
  V8 runtime inclusion and a V8-enabled smoke lane records evidence.

## Package and Release Limits

- Packages are experimental development artifacts.
- No installer, signing, notarization, auto-update, Homebrew, winget, npm wrapper, or
  public GitHub release is included in the dry-run.
- Package smoke validates outside-checkout layout and basic CLI behavior; it is not
  production readiness evidence.
- Live provider behavior requires separate opt-in evidence.
- TLS hardening, framework v2 product-mode behavior, and final public docs remain outside
  this release dry-run template.

## Deferred Release Work

- Hosted public V8 SDK artifact source and retention policy.
- Signing, notarization, installers, and package-manager distribution.
- Final public alpha release notes after the readiness gate accepts the evidence.
- Final alpha verification and product-mode cutover.
