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
.\tools\windows\dev.ps1 package
.\tools\windows\dev.ps1 test-package
.\tools\windows\dev.ps1 npm-dry-run
.\tools\windows\dev.ps1 dogfood
```

The root `tools/*.ps1` files forward here as convenience entrypoints.

For memory-sensitive changes, `lint` remains the fast default gate and `analyze` is the
controlled memory/core clang-tidy/Clang Static Analyzer lane:

```powershell
.\tools\windows\dev.ps1 configure
.\tools\windows\dev.ps1 analyze
```

`analyze` requires `compile_commands.json` from configure and a local `clang-tidy`. It
builds the `sloppy_memory_analysis` target; the broader `sloppy_clang_tidy` target is
exploratory until the full-repo analyzer baseline is quiet. Report it as the `advanced
static analysis` lane, separate from default non-V8 validation.

## V8 SDK Discovery

V8 is optional for the default developer loop. When a V8-enabled build is needed, use the
shared resolver instead of hard-coding one local path:

```powershell
.\tools\windows\resolve-v8-sdk.ps1 -Fetch
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

Windows x64 has a pinned, checksum-validated SDK artifact source. `fetch-v8.ps1` stores
the downloaded archive under `.sdeps/v8/_downloads`, extracts the SDK into
`.sdeps/v8/windows-x64`, validates `share/sloppy-v8-sdk.json`, and refuses checksum or
layout mismatches. `dev.ps1 configure -EnableV8` also provisions the SDK when no
compatible local SDK is found. Linux and macOS SDK artifact sources remain planned and
must be reported under their own status.

Validation uses the same helper without downloading:

```powershell
.\tools\windows\fetch-v8.ps1 -ValidateOnly
```

Maintainers can build and package a local SDK with `build-v8.ps1`.

Experimental local packaging lives here too:

```powershell
.\tools\windows\dev.ps1 package -Preset windows-release
.\tools\windows\dev.ps1 test-package
```

The package script stages the alpha ZIP layout under ignored `artifacts/packages/`, writes
`SHA256SUMS.txt`, and can smoke-test the extracted archive outside the checkout. The smoke
checks packaged CLI startup, `sloppy doctor`, required package docs, stdlib assets,
examples, manifest fields, prebuilt artifact execution without compiling source, and
honest non-V8 `sloppy run --artifacts` skip reporting. It does not install anything,
mutate PATH, fetch V8, include a V8 SDK, sign artifacts, or publish a release.

Manual release artifact dry-runs add release policy checks around the same
package/test-package path:

```powershell
.\tools\windows\check-release-artifacts.ps1
.\tools\windows\release-dry-run.ps1 -Preset windows-release
```

The dry-run writes ignored output under `artifacts/release-dry-run/`, verifies
`SHA256SUMS.txt`, and does not require secrets or publish a GitHub release.

npm package dry-runs generate `@sloppy/runtime` and the matching platform package from an
already-built archive:

```powershell
.\tools\windows\dev.ps1 npm-dry-run -PackagePath artifacts\packages\sloppy-windows-x64.zip
```

The npm package path is an installer/launcher for Sloppy itself. It does not add npm app
dependency support, `node_modules` resolution, `node-gyp`, native postinstall builds, or V8
downloads during install.

Dogfood evidence is cataloged in `examples/dogfood/dogfood.json` and can be
reported through:

```powershell
.\tools\windows\dogfood.ps1 -StatusOnly
.\tools\windows\dev.ps1 dogfood -Preset windows-relwithdebinfo -EnableV8
```

`dogfood.ps1` reuses the existing source-input, pre-alpha control-plane, and package
smoke harnesses. Without a V8-enabled build it reports V8-required examples as
unavailable diagnostics, not positive execution. Without `-PackagePath` it reports
package-mode dogfood as skipped.

Local benchmark evidence is reported through `bench.ps1`:

```powershell
.\tools\windows\bench.ps1 -List
.\tools\windows\bench.ps1 -Smoke -Json
.\tools\windows\bench.ps1 -Suite http -Runtime sloppy,node,bun,deno -Out artifacts\bench\local-comparison.json
.\tools\windows\bench.ps1 -Compare @("artifacts\bench\before.json", "artifacts\bench\after.json")
```

`-List` and `-Smoke` exercise the native `sloppy_bench` harness. `-Suite` uses the BENCH-01
local runtime runner for internal branch-to-branch measurements. Missing Node, Bun, Deno,
or V8-enabled Sloppy executables are reported in the JSON as unavailable lanes.
