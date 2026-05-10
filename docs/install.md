# Install

The public alpha package is `@rtlzeromemory/sloppy`.

```sh
npm install -g @rtlzeromemory/sloppy@alpha
```

The root package installs a small launcher plus the matching platform package
through npm optional dependencies. It does not build native code during
install.

## Verify the install

```sh
sloppy --version
sloppy doctor
```

`sloppy --version` prints the runtime CLI version. `sloppy doctor` reports
what the local install can do, including whether the runtime can execute
JavaScript handlers (V8) and which native libraries (`libpq`, ODBC, OpenSSL)
are present.

A first end-to-end smoke test:

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

| Platform | Architecture | Alpha package status |
| --- | --- | --- |
| Windows | x64 | published npm platform package with V8-backed handler execution |
| Linux (glibc) | x64 | published npm platform package with V8-backed handler execution |
| macOS | x64, arm64 | published npm platform package with V8-backed handler execution |
| Linux | arm64 | source/archive builds |
| Windows | arm64 | source/archive builds |

For the live status of each platform's coverage, see
[Platform status](reference/platform-status.md).

## Troubleshoot

- **`sloppy: command not found`.** Add the npm global prefix to `PATH`. On
  Linux/macOS, `npm config get prefix` shows the bin directory; on Windows
  the launcher is under `%AppData%\npm`.
- **No matching platform binary.** If `npm install -g @rtlzeromemory/sloppy@alpha`
  reports no matching optional dependency for your platform, install on a
  supported platform from the table above or use a source build. The launcher
  prints a clear diagnostic when the platform package is missing.
- **Confirm the binary path.** `command -v sloppy` (POSIX) or `where sloppy`
  (Windows) shows which launcher is on `PATH`.
- **Windows `.cmd` launcher issues.** Run
  `node <install>/node_modules/@rtlzeromemory/sloppy/bin/sloppy.js --version`
  to separate npm shim problems from Sloppy problems.
- **`sloppy run` reports a V8 requirement.** The launcher is present but the
  installed runtime cannot execute handlers. Reinstall the alpha platform
  package, or use a V8-enabled source build.
- **`node_modules` imports fail.** Install the package with your package
  manager first, then check whether the package is compatible with Sloppy's
  bundled dependency graph. See [Using installed packages](guide/using-packages.md).

## Build from source

Use a source build when you are working on Sloppy itself, testing an
unpublished change, or using a platform without an npm package.

Windows x64:

```powershell
.\tools\windows\bootstrap.ps1
.\tools\windows\dev.ps1 doctor
.\tools\windows\resolve-v8-sdk.ps1
.\tools\windows\dev.ps1 configure -Preset windows-relwithdebinfo -EnableV8
.\tools\windows\dev.ps1 build -Preset windows-relwithdebinfo
```

Linux x64 and macOS:

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

Archives are useful for release testing and for installing a local build
without publishing to npm.

Windows x64:

```powershell
.\tools\windows\dev.ps1 package -Preset windows-relwithdebinfo -EnableV8
.\tools\windows\dev.ps1 test-package -Preset windows-relwithdebinfo -EnableV8
.\tools\windows\dev.ps1 test-install -Preset windows-relwithdebinfo -EnableV8
```

Linux x64 and macOS:

```sh
./tools/unix/dev.sh package --enable-v8
./tools/unix/dev.sh test-package --require-v8-runtime
./tools/unix/dev.sh test-install --require-v8-runtime
```

Extracted packages include `bin/`, `stdlib/`, `templates/`, selected docs and
examples, and `manifest.json`. SDK headers and import libraries are not part
of the runtime package.

## V8 and handler execution

`sloppy build`, `routes`, `capabilities`, `doctor`, `audit`, `openapi`, and
`package` read metadata and artifacts. `sloppy run` executes handlers, so it
needs a V8-enabled runtime package or source build.

If `sloppy run` reports that a V8-enabled build is required, the CLI is
present but that runtime cannot execute handlers.

Next: [Quickstart](quickstart.md).
