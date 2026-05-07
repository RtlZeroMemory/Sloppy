# Unix Tools

Linux/macOS shell tooling belongs here.

Default Linux/macOS CI uses direct CMake/Cargo commands plus these lightweight scanners:

```sh
tools/unix/check-platform-boundaries.sh
tools/unix/check-c-standards.sh --self-test
tools/unix/check-c-standards.sh
```

They mirror the boundary intent of the Windows PowerShell scanners for hosted POSIX
runners. Windows remains the complete local developer workflow through `tools/windows`.

`package.sh` is the first experimental TAR packaging path:

```sh
tools/unix/package.sh --configuration Release
```

It stages the same local archive layout as the Windows ZIP script and writes a `.tar.gz`
plus `SHA256SUMS.txt` under ignored `artifacts/packages/`. This script is intentionally
simple and is not part of the required default CI gate yet; hosted Linux/macOS CI currently
validates configure, build, test, Cargo gates, and standards scanners.

`test-package.sh` is the matching outside-checkout package-layout smoke:

```sh
tools/unix/test-package.sh --package-path artifacts/packages/sloppy-0.0.0-dev-linux-x64.tar.gz
```

It extracts the archive under the system temp directory, runs `sloppy --version`,
`sloppy --help`, `sloppyc --version`, and `sloppyc --help`, verifies stdlib assets,
manifest fields, required package files, excluded build/dependency directories, packaged
`sloppyc build` from the extracted layout, and `SHA256SUMS.txt` when present. Default
non-V8 packages report packaged `sloppy run --artifacts` as skipped/not configured because
V8 is unavailable. It is not part of the required CI gate until a scoped package-smoke job
is added.
