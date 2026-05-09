# How To Install Sloppy

Install Sloppy from a local Windows package archive and verify both CLIs.

## Prerequisites

- A local `sloppy-windows-x64.zip` package archive.
- PowerShell 7 or Windows PowerShell.

## Steps

1. Extract the archive outside the repository checkout.

```powershell
$zip = "<repo>\artifacts\packages\sloppy-windows-x64.zip"
$installRoot = "<install-root>"
Expand-Archive -LiteralPath $zip -DestinationPath $installRoot -Force
```

2. Add the package `bin` directory to your current shell `PATH`.

```powershell
$env:Path = "$installRoot\sloppy-windows-x64\bin;$env:Path"
```

3. Verify the runtime and compiler CLIs.

```powershell
sloppy --version
sloppyc --version
sloppy --help
```

## Expected Result

- `sloppy --version` prints `Sloppy <version>`.
- `sloppyc --version` prints `sloppyc <version>`.
- `sloppy --help` prints command usage for `build`, `run`, `doctor`, `routes`, `capabilities`, `audit`, and `openapi`.

## Common Failures

- `sloppy` or `sloppyc` is not recognized: `PATH` does not include the extracted `bin` directory.
- Missing `sloppy.exe` or `sloppyc.exe`: the archive extraction path is wrong, or the archive is incomplete.
- Expecting npm app dependency behavior: Sloppy runtime packaging does not add `node_modules` resolution or Node compatibility for apps.
