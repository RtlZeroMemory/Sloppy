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
