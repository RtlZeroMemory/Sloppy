# How To Install Sloppy Locally

Install Sloppy from a local package archive and verify the runtime CLI.

## Prerequisites

- A Sloppy package archive built or downloaded for your platform.
- A shell where you can add the archive `bin` directory to `PATH`.

Current package status:

- Windows x64 local archives are the most validated local package path.
- Linux archives exist as an experimental package lane when built by the release
  scripts.
- macOS and Windows arm64 package/runtime lanes are not currently the primary
  local install path.
- The npm launcher package is not a general runtime package manager for Sloppy
  apps. Treat npm app dependency behavior as planned/not available unless a
  scoped package task says otherwise.

## Steps

1. Choose the archive for your platform.

| Platform | Archive name |
| --- | --- |
| Windows x64 | `sloppy-windows-x64.zip` |
| Linux x64 | `sloppy-linux-x64.tar.gz` |

2. Extract the archive outside the repository checkout.

Windows:

```powershell
$zip = "<repo>\artifacts\packages\sloppy-windows-x64.zip"
$installRoot = "<install-root>"
Expand-Archive -LiteralPath $zip -DestinationPath $installRoot -Force
```

Linux:

```sh
tar -xzf <repo>/artifacts/packages/sloppy-linux-x64.tar.gz -C <install-root>
```

3. Add the package `bin` directory to your current shell `PATH`.

Windows:

```powershell
$env:Path = "$installRoot\sloppy-windows-x64\bin;$env:Path"
```

Linux:

```sh
export PATH="<install-root>/sloppy-linux-x64/bin:$PATH"
```

4. Verify the runtime and compiler CLIs.

```powershell
sloppy --version
sloppyc --version
sloppy --help
```

## Expected Result

- `sloppy --version` prints `Sloppy <version>`.
- `sloppyc --version` prints `sloppyc <version>`.
- `sloppy --help` prints command usage for `build`, `run`, `doctor`, `routes`, `capabilities`, `audit`, and `openapi`.

## Contributor Source Builds

Repository source builds are not the normal user install path. Contributors who
need a local binary from source should use
[Building from source](../contributor/building-from-source.md), then either run
the built executable directly or package it with
[Package Sloppy locally](package-sloppy.md).

## Common Failures

- `sloppy` or `sloppyc` is not recognized: `PATH` does not include the extracted `bin` directory.
- Missing `sloppy.exe` or `sloppyc.exe`: the archive extraction path is wrong, or the archive is incomplete.
- Expecting npm-style app dependencies: Sloppy runtime packaging does not provide `node_modules` resolution for application code.
