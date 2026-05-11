# Install

The public alpha, pre-production package is `@rtlzeromemory/sloppy`.

```sh
npm install -g @rtlzeromemory/sloppy@alpha
```

Check the install:

```sh
sloppy --version
sloppy --help
```

Create and run a minimal app:

```sh
sloppy create my-api --template minimal-api
cd my-api
sloppy build
sloppy run .sloppy --once GET /health
```

Expected response body:

```text
ok
```

## Platforms

| Platform | Alpha package status |
| --- | --- |
| Windows x64 | npm platform package with V8-backed handler execution |
| Linux x64 glibc | npm platform package with V8-backed handler execution |
| macOS arm64 | supported macOS alpha lane with V8-backed handler execution |
| macOS x64 | supported macOS alpha lane with V8-backed handler execution |
| Linux arm64 | no alpha npm platform package; source build only |
| Windows arm64 | no alpha npm platform package; source build only |

The root package installs a small launcher plus the matching supported
platform package through npm optional dependencies. It does not build native
code during install, run `node-gyp`, or download V8 in `postinstall`.

## Build from source

Use a source build when you are working on Sloppy itself, testing an unpublished
change, or using Linux arm64, Windows arm64, or another platform without an
alpha package.

Windows x64:

```powershell
.\tools\windows\bootstrap.ps1
.\tools\windows\dev.ps1 doctor
.\tools\windows\resolve-v8-sdk.ps1
.\tools\windows\dev.ps1 configure -Preset windows-relwithdebinfo -EnableV8
.\tools\windows\dev.ps1 build -Preset windows-relwithdebinfo
```

Linux x64:

```sh
./tools/unix/bootstrap.sh
./tools/unix/dev.sh doctor
./tools/unix/dev.sh build-v8
./tools/unix/dev.sh configure --enable-v8
./tools/unix/dev.sh build
```

Full toolchain notes live in
[Building from source](contributor/building-from-source.md).

## Build a local archive

Archives are useful for release testing and for installing a local build without
publishing to npm.

Windows x64:

```powershell
.\tools\windows\dev.ps1 package -Preset windows-relwithdebinfo -EnableV8
.\tools\windows\dev.ps1 test-package -Preset windows-relwithdebinfo -EnableV8
.\tools\windows\dev.ps1 test-install -Preset windows-relwithdebinfo -EnableV8
```

Linux x64:

```sh
./tools/unix/dev.sh package --enable-v8
./tools/unix/dev.sh test-package --require-v8-runtime
./tools/unix/dev.sh test-install --require-v8-runtime
```

Extracted packages include `bin/`, `stdlib/`, `templates/`, selected docs and
examples, and `manifest.json`. SDK headers and import libraries are not part of
the runtime package.

## V8 and handler execution

`sloppy build`, `routes`, `capabilities`, `doctor`, `audit`, `openapi`, and
`package` read metadata and artifacts. `sloppy run` executes handlers, so it
needs a V8-enabled runtime package or source build.

If `sloppy run` reports that a V8-enabled build is required, the CLI is present
but that runtime cannot execute handlers.

The normal Quickstart does not require extra native setup. SQLite is included
with the runtime package. V8 is part of the V8-enabled runtime package.
PostgreSQL client support is only needed for apps that use the PostgreSQL
provider or PostgreSQL migrations. Microsoft ODBC Driver 17 or 18 is only
needed for apps that use the SQL Server provider or SQL Server migrations.
Declared native libraries are only needed for FFI apps, and OpenSSL/TLS matters
only for TLS/HTTPS paths in the selected build/package configuration.

Current alpha packages do not bundle PostgreSQL provider-package binaries yet.
SQL Server uses Microsoft's platform driver or an organization-managed ODBC
deployment; Sloppy does not bundle Microsoft's ODBC driver in the core package.

## Common fixes

- `sloppy: command not found`: add the npm global prefix to `PATH`.
- Windows `.cmd` launcher issues: run `node <install>/node_modules/@rtlzeromemory/sloppy/bin/sloppy.js --version` to separate npm shim problems from Sloppy problems.
- `node_modules` imports fail: install the package with your package manager
  first, then check whether the package is compatible with Sloppy's bundled
  dependency graph. See [Using installed packages](guide/using-packages.md).

Next: [Quickstart](quickstart.md).
