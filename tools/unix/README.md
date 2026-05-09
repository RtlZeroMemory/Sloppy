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
tools/unix/dev.sh dogfood
tools/unix/check-platform-boundaries.sh
tools/unix/check-c-standards.sh --self-test
tools/unix/check-c-standards.sh
```

The Unix `bootstrap.sh` and `dev.sh` command contract mirrors the Windows vocabulary for
Linux and macOS. Unsupported optional lanes are reported as unavailable. Linux x64 V8 does not use distro Node/V8 development packages; it uses the
Sloppy-owned SDK produced by `tools/unix/build-v8.sh` or an extracted matching SDK
artifact.

On Debian/Ubuntu-style Linux containers or hosts, install the current non-V8 Linux clang
lane prerequisites before running bootstrap:

```sh
apt-get install -y --no-install-recommends \
  git cmake ninja-build curl zip unzip tar file pkg-config build-essential clang \
  ca-certificates perl bison flex autoconf autoconf-archive automake libtool \
  m4 python3 gawk lld libglib2.0-dev cargo
```

Use a Rust toolchain new enough for the compiler dependencies. The current Oxc dependency
set requires Rust 1.93.0 or newer.

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

Linux x64 V8 package evidence uses the pinned Sloppy-owned SDK:

```sh
tools/unix/dev.sh build-v8
tools/unix/dev.sh package --enable-v8
tools/unix/dev.sh test-package --require-v8-runtime
```

`build-v8` fetches/builds the pinned V8 revision with depot_tools, packages
`.sdeps/v8/linux-x64`, validates the SDK manifest, and writes a reusable SDK archive under
ignored `artifacts/v8-sdk/`. The package script excludes SDK headers and libraries from
the user archive; static SDK builds link V8 into `bin/sloppy`, while shared SDK builds
bundle only the required runtime shared libraries under `engines/v8`. The Linux x64 SDK is
Sloppy-built with V8's Chromium libc++ support and sandbox-enabled ABI metadata generated
from V8's own `v8_features.json`; it does not depend on distro Node/V8 development
packages. V8-enabled Linux source builds use the depot_tools LLVM toolchain from
`.sdeps/v8-work/v8/third_party/llvm-build/Release+Asserts`; set `SLOPPY_V8_LLVM_ROOT`
when the SDK was built with a non-default `--work-root`.

Manual Unix release artifact dry-runs wrap the same package smoke path:

```sh
tools/unix/release-dry-run.sh --preset linux-clang
```

The dry-run writes ignored evidence under `artifacts/release-dry-run/`, verifies package
checksums through `test-package.sh`, and does not require secrets or publish a
release.

Dogfood status is represented through:

```sh
tools/unix/dogfood.sh
tools/unix/dev.sh dogfood
```

The Unix dogfood script validates the shared catalog and can run package-mode smoke when a
TAR archive is supplied. Positive source-input and pre-alpha control-plane execution remain
V8-gated and must be reported separately from this static Unix lane.

`tools/unix/dev.sh npm-dry-run` currently reports unavailable instead of faking a Unix npm
package generator. The committed npm package metadata is platform-neutral, but the local
dry-run generator in this PR is the Windows `tools/windows/npm-dry-run.ps1` path. A Unix
generator should reuse tested archive contents and preserve the native-install-script
policy before being reported as available.

Provider live lanes have POSIX wrappers for machines that already have the matching CMake
preset configured and the required service dependencies installed:

```sh
tools/unix/test-live-postgres.sh --preset linux-clang
tools/unix/test-live-sqlserver.sh --preset linux-clang
tools/unix/test-live-providers.sh --provider all --preset linux-clang
```

These scripts are opt-in Docker-backed live-provider lanes. Missing Docker, missing
ODBC driver support, or an unavailable database service is reported as unavailable or
failed live-provider status, never as default CI success.
