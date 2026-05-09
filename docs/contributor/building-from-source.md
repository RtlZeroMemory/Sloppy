# Building from source

Sloppy is C + Rust + C++ (V8 bridge). Building from source means CMake
for the runtime, Cargo for the compiler, and an optional V8 SDK for
runtime execution.

Windows x64 is the most validated lane. Linux x64 works; macOS and arm64
need work.

## Windows

From a PowerShell that has Visual Studio's C++ tools on `PATH` (a
Developer PowerShell prompt, or a regular shell after running
`vcvarsall.bat`):

```powershell
.\tools\windows\bootstrap.ps1
.\tools\windows\dev.ps1 doctor
.\tools\windows\dev.ps1 configure
.\tools\windows\dev.ps1 build
.\tools\windows\dev.ps1 test
```

`bootstrap.ps1` fetches dependencies into `.sdeps/`. `doctor` reports
what's available. `configure` defaults to the `windows-dev` preset
(non-V8). The built binaries live under `build\<preset>\`.

To build with V8 enabled (required to run handlers):

```powershell
.\tools\windows\resolve-v8-sdk.ps1 -Fetch
.\tools\windows\dev.ps1 configure -Preset windows-relwithdebinfo -EnableV8
.\tools\windows\dev.ps1 build -Preset windows-relwithdebinfo
.\tools\windows\dev.ps1 test -Preset windows-relwithdebinfo
```

`dev.ps1` rejects `windows-dev` + `-EnableV8` because the V8 SDK is
RelWithDebInfo-compatible.

Other handy commands:

```powershell
.\tools\windows\dev.ps1 help            # all commands and flags
.\tools\windows\dev.ps1 clean           # remove build/<preset> only
.\tools\windows\dev.ps1 format-check    # clang-format / cargo fmt
.\tools\windows\dev.ps1 lint            # standards + boundary scans
.\tools\windows\dev.ps1 analyze         # advanced static analysis
.\tools\windows\dev.ps1 package         # local package archive
.\tools\windows\dev.ps1 test-package    # smoke the package outside the checkout
```

## Linux

```sh
./tools/unix/bootstrap.sh
./tools/unix/dev.sh doctor
./tools/unix/dev.sh configure
./tools/unix/dev.sh build
./tools/unix/dev.sh test
```

Same command shape. Unix wrappers are thinner than the Windows ones —
some Windows-only commands aren't implemented yet.

## What you need installed

| Tool         | Purpose                                                   |
| ------------ | --------------------------------------------------------- |
| CMake ≥ 3.25 | Build system                                              |
| Ninja        | Default generator                                         |
| Clang/MSVC   | C/C++ compiler (clang-cl on Windows is the default)       |
| Rust toolchain | Compiler (`rustup` is fine; latest stable)              |
| Git          | Submodule fetches in `.sdeps`                             |
| Python 3     | Some helper scripts                                       |

`tools/windows/dev.ps1 doctor` (and the Unix equivalent) reports what's
missing.

## Optional dependencies

Picked up automatically when present, ignored when absent:

| Dependency  | Enables                              |
| ----------- | ------------------------------------ |
| V8 SDK      | JavaScript handler execution         |
| OpenSSL     | Inbound TLS                          |
| libpq       | PostgreSQL provider                  |
| ODBC driver | SQL Server provider                  |
| libsodium   | Password hashing                     |
| vcpkg       | Convenience for native deps on Windows |

Set `VCPKG_ROOT` if you want to use a vcpkg root other than the one
`.sdeps/vcpkg` would bootstrap.

## Environment variables

| Variable                             | Purpose                                            |
| ------------------------------------ | -------------------------------------------------- |
| `VCPKG_ROOT`                         | Override default vcpkg root                        |
| `SLOPPY_V8_ROOT` / `-V8Root`         | Explicit V8 SDK root for V8 builds                 |
| `SLOPPY_POSTGRES_TEST_URL`           | Live PostgreSQL provider test URL                  |
| `SLOPPY_SQLSERVER_TEST_CONNECTION_STRING` | Live SQL Server connection string             |

Don't commit credentials. The `.gitignore` covers obvious local files;
the rest is on you.

## Output layout

```
build/
  <preset>/
    sloppy.exe          (or sloppy on Unix)
    sloppyc.exe
    libsloppy_*.{a,lib}
    tests/...

compiler/
  target/               Cargo build output

artifacts/
  packages/             local package archives from `package`
```

Adding `build\<preset>\` to your `PATH` lets you invoke `sloppy` from
anywhere as if you'd installed it.

## When something goes wrong

Run `sloppy doctor` against your built binary first — it surfaces most
environment issues. If the build itself is broken, `dev.ps1 doctor`
reports the toolchain side; CMake errors generally include a clear
"missing X" line.

For V8 SDK resolution issues specifically, see
[v8-sdk.md](v8-sdk.md).
