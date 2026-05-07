# Unix Tools

Linux/macOS shell tooling belongs here.

Default Linux/macOS CI uses direct CMake/Cargo commands plus these lightweight scanners:

```sh
tools/unix/bootstrap.sh
tools/unix/dev.sh doctor
tools/unix/dev.sh configure
tools/unix/dev.sh build
tools/unix/dev.sh test
tools/unix/dev.sh lint
tools/unix/dev.sh package
tools/unix/dev.sh test-package
tools/unix/check-platform-boundaries.sh
tools/unix/check-c-standards.sh --self-test
tools/unix/check-c-standards.sh
```

The Unix `bootstrap.sh` and `dev.sh` command contract mirrors the Windows vocabulary for
Linux and macOS. Unsupported optional lanes, including Unix V8 SDK artifact fetch, are
reported as unavailable rather than pass evidence.

`package.sh` is the first experimental TAR packaging path:

```sh
tools/unix/dev.sh package
```

It stages the same local archive layout as the Windows ZIP script and writes a `.tar.gz`
plus `SHA256SUMS.txt` under ignored `artifacts/packages/`. This script is intentionally
simple; hosted Linux/macOS default CI validates configure, build, test, Cargo gates, and
standards scanners unless a package-specific lane is enabled.

`test-package.sh` is the matching outside-checkout package-layout smoke:

```sh
tools/unix/dev.sh test-package
```

It extracts the archive under the system temp directory, runs `sloppy --version`,
`sloppy --help`, `sloppy doctor`, `sloppyc --version`, and `sloppyc --help`, verifies
stdlib assets, package docs, examples, manifest fields, required package files, excluded
build/dependency directories, packaged `sloppyc build` from the extracted layout, and
`SHA256SUMS.txt` when present. Default non-V8 packages report packaged
`sloppy run --artifacts` as skipped/not configured because V8 is unavailable.

Provider live lanes have POSIX wrappers for machines that already have the matching CMake
preset configured and the required service dependencies installed:

```sh
tools/unix/test-live-postgres.sh --preset linux-clang
tools/unix/test-live-sqlserver.sh --preset linux-clang
tools/unix/test-live-providers.sh --provider all --preset linux-clang
```

These scripts are opt-in Docker-backed live-provider evidence. Missing Docker, missing
ODBC driver support, or an unavailable database service is reported as unavailable or
failed live-provider evidence, never as default CI success.
