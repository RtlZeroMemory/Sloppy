# How To Package Sloppy

Create and test an experimental Windows package archive.

## Prerequisites

- Windows checkout with `tools/windows/dev.ps1`.
- Built toolchain dependencies needed by the selected preset.

## Steps

1. Create the package archive.

```powershell
.\tools\windows\dev.ps1 package -Preset windows-relwithdebinfo
```

2. Smoke-test the newest package archive.

```powershell
.\tools\windows\dev.ps1 test-package -Preset windows-relwithdebinfo
```

3. Optional explicit archive path.

```powershell
.\tools\windows\dev.ps1 test-package -PackagePath artifacts\packages\sloppy-windows-x64.zip -PackageMetadataPath tests\fixtures\package\windows-default\case.json
```

## Expected Result

- `artifacts/packages/sloppy-windows-x64.zip` is created.
- `artifacts/packages/SHA256SUMS.txt` is created.
- The package test extracts the archive outside the checkout and validates
  layout, checksums, and CLI behavior.

## Common Failures

- `No Windows package archive found under ...`: run `dev.ps1 package` first.
- `Required package input is missing`: runtime/compiler binaries were not built for the selected configuration.
- `requires V8-enabled build` during package testing: artifact execution is not
  V8-enabled in that package.
- Assuming npm app dependency support: packaging does not add `node_modules` support for Sloppy apps.
