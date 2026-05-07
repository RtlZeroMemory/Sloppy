# Windows Tools

This directory contains the most complete validated local Windows developer scripts.

Run from a Visual Studio Developer PowerShell/Command Prompt or a normal PowerShell with
Visual Studio C++ tools installed. The scripts import the MSVC/Windows SDK environment when
needed:

```powershell
.\tools\windows\bootstrap.ps1
.\tools\windows\dev.ps1 doctor
.\tools\windows\dev.ps1 configure
.\tools\windows\dev.ps1 build
.\tools\windows\dev.ps1 test
.\tools\windows\dev.ps1 format-check
.\tools\windows\dev.ps1 lint
```

The root `tools/*.ps1` files forward here for compatibility.

For memory-sensitive changes, `lint` remains the fast default gate and `analyze` is the
controlled memory/core clang-tidy/Clang Static Analyzer lane:

```powershell
.\tools\windows\dev.ps1 configure
.\tools\windows\dev.ps1 analyze
```

`analyze` requires `compile_commands.json` from configure and a local `clang-tidy`. It
builds the `sloppy_memory_analysis` target; the broader `sloppy_clang_tidy` target is
exploratory until the full-repo analyzer baseline is quiet. Report it as the `advanced
static analysis` lane, separate from default non-V8 evidence.

## V8 SDK Discovery

V8 is optional for the default developer loop. When a V8-enabled build is needed, use the
shared resolver instead of hard-coding one local path:

```powershell
.\tools\windows\resolve-v8-sdk.ps1
.\tools\windows\dev.ps1 configure -Preset windows-relwithdebinfo -EnableV8
```

Dependency and platform policy is tracked in `tools/deps/sloppy-deps.json`. The V8
resolver supports `OFF`, `AUTO`, and `REQUIRED` modes. It checks, in order, an explicit
`-V8Root`, `SLOPPY_V8_ROOT`, `SLOPPY_V8_SDK_HINTS`, this worktree's
`.sdeps/v8/windows-x64`, and the same `.sdeps` location in registered git worktrees.
`SLOPPY_V8_ROOT` is an advanced override, not the default contributor path.
`SLOPPY_V8_SDK_HINTS` is a path-list environment variable separated by the platform path
separator, so agents can point at portable cache roots without baking machine-local paths
into docs or PRs.

Validation uses the same helper:

```powershell
.\tools\windows\fetch-v8.ps1 -ValidateOnly
```

`fetch-v8.ps1` still downloads nothing; it validates or explains the expected SDK layout.
Maintainers can build and package a local SDK with `build-v8.ps1`.

Experimental local packaging lives here too:

```powershell
.\tools\windows\package.ps1 -Configuration Release
.\tools\windows\package.ps1 -Configuration Release -Smoke
.\tools\windows\test-package.ps1 -PackagePath artifacts\packages\sloppy-0.0.0-dev-windows-x64.zip
```

The package script stages a ZIP under ignored `artifacts/packages/`, writes
`SHA256SUMS.txt`, and can smoke-test the extracted archive outside the checkout. The smoke
checks packaged CLI startup, required files and stdlib assets, packaged `sloppyc build`,
and honest non-V8 `sloppy run --artifacts` skip reporting. It does not install anything,
mutate PATH, fetch V8, include a V8 SDK, sign artifacts, or create a public release.
