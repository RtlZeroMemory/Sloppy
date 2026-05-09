# Building From Source

Sloppy is cross-platform by design. Windows x64 is the most complete validated
local contributor workflow today. These instructions are for contributors working
inside the repository. Product tutorials assume an installed or extracted
`sloppy` executable instead.

## Windows x64 Default Build

From the repository root:

```powershell
.\tools\windows\bootstrap.ps1
.\tools\windows\dev.ps1 doctor
.\tools\windows\dev.ps1 configure
.\tools\windows\dev.ps1 build
.\tools\windows\dev.ps1 test
```

Notes:

- `configure` defaults to `windows-dev`.
- The default build is non-V8 unless V8 is explicitly enabled.
- `test` runs CTest for the selected preset and compiler tests when Cargo is
  available.

## Windows V8 Build

Run this separately for runtime/app-host/compiler/provider/configuration/V8 work:

```powershell
.\tools\windows\resolve-v8-sdk.ps1
.\tools\windows\dev.ps1 configure -Preset windows-relwithdebinfo -EnableV8
.\tools\windows\dev.ps1 build -Preset windows-relwithdebinfo
.\tools\windows\dev.ps1 test -Preset windows-relwithdebinfo
```

`dev.ps1` rejects `windows-dev` + `-EnableV8` because the local V8 SDK is
release/RelWithDebInfo compatible.

## Useful Windows Commands

```powershell
.\tools\windows\dev.ps1 help
.\tools\windows\dev.ps1 clean
.\tools\windows\dev.ps1 format-check
.\tools\windows\dev.ps1 lint
.\tools\windows\dev.ps1 package
.\tools\windows\dev.ps1 test-package
.\tools\windows\dev.ps1 dogfood
.\tools\windows\dev.ps1 analyze
```

Use `.\tools\windows\dev.ps1 help` for command options and parameter details.

## Unix Workflow

Unix wrappers use the same command shape where implemented. Linux x64 is the
active Unix target for local package/runtime work; macOS support is still less
validated than the Windows workflow.

```sh
./tools/unix/bootstrap.sh
./tools/unix/dev.sh doctor
./tools/unix/dev.sh configure
./tools/unix/dev.sh build
./tools/unix/dev.sh test
```

## Common Environment Variables

| Variable | Use |
| --- | --- |
| `VCPKG_ROOT` | Optional vcpkg root when `.sdeps/vcpkg` is not bootstrapped. |
| `SLOPPY_V8_ROOT` or `-V8Root` | Explicit V8 SDK root when using V8 scripts. |
| `SLOPPY_POSTGRES_TEST_URL` | Live PostgreSQL provider test URL. |
| `SLOPPY_SQLSERVER_TEST_CONNECTION_STRING` | Live SQL Server ODBC connection string. |
| `Sloppy__Providers__postgres__main__connectionString` | App/provider config override for PostgreSQL `main`. |
| `Sloppy__Providers__sqlserver__main__connectionString` | App/provider config override for SQL Server `main`. |

Never commit real credentials.

## Reporting

For each check or build configuration, report one of `PASS`, `FAIL`, `SKIPPED`, `UNAVAILABLE`,
`DEFERRED`, or `NOT RUN`.

Never report success for a command you did not run.
