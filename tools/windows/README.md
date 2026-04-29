# Windows Tools

This directory contains the first-class Windows developer scripts.

Run from a Visual Studio Developer PowerShell/Command Prompt or a normal PowerShell with
Visual Studio C++ tools installed. The scripts import the MSVC/Windows SDK environment when
needed:

```powershell
.\tools\windows\bootstrap.ps1
.\tools\windows\dev.ps1 configure
.\tools\windows\dev.ps1 build
.\tools\windows\dev.ps1 test
.\tools\windows\dev.ps1 format-check
.\tools\windows\dev.ps1 lint
```

The root `tools/*.ps1` files forward here for compatibility.

Experimental local packaging lives here too:

```powershell
.\tools\windows\package.ps1 -Configuration Release
.\tools\windows\package.ps1 -Configuration Release -Smoke
.\tools\windows\test-package.ps1 -PackagePath artifacts\packages\sloppy-0.0.0-dev-windows-x64.zip
```

The package script stages a ZIP under ignored `artifacts/packages/`, writes
`SHA256SUMS.txt`, and can smoke-test the extracted archive outside the checkout. It does
not install anything, mutate PATH, fetch V8, include a V8 SDK, sign artifacts, or create a
public release.
