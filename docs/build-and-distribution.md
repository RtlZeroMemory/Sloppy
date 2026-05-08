# Build And Distribution

## Purpose

This document describes how Sloppy builds local development artifacts today and what it
does not yet claim for packaging or release.

## Current Build Outputs

The repository builds:

- the native `sloppy` runtime CLI;
- the Rust `sloppyc` compiler CLI;
- optional V8-backed runtime code when the V8 SDK gate is enabled;
- test, conformance, and benchmark harness binaries where configured;
- experimental package layouts and package fixtures.

The default build keeps optional V8 behavior gated. A default build/test pass is not V8,
package release, live-provider, production HTTP, or benchmark evidence.

## Windows Workflow

Canonical local workflow:

```powershell
.\tools\windows\bootstrap.ps1
.\tools\windows\dev.ps1 doctor
.\tools\windows\dev.ps1 configure
.\tools\windows\dev.ps1 build
.\tools\windows\dev.ps1 test
.\tools\windows\dev.ps1 lint
.\tools\windows\dev.ps1 package
.\tools\windows\dev.ps1 test-package
.\tools\windows\dev.ps1 dogfood
```

V8-enabled work must run the resolver and V8 preset when the task touches runtime,
app-host, compiler, bootstrap, provider, configuration, or V8-adjacent behavior and the
local resolver succeeds.

## Dependencies

C/C++ dependencies are managed through the repository's CMake/vcpkg workflow. The
dependency, platform, feature, and V8 SDK policy source of truth is
`tools/deps/sloppy-deps.json`. `tools/windows/dev.ps1 doctor` reports required and optional
dependencies with stable statuses: `found`, `missing`, `wrong version`,
`unsupported platform`, `optional unavailable`, and `corrupt dependency`.

V8 SDK resolution is explicit and mode-aware: `OFF` disables SDK validation, `AUTO`
reports a compatible SDK when present without counting absence as V8 evidence, and
`REQUIRED` fails when the SDK is missing, wrong, or corrupt. Windows x64 has a pinned,
checksum-validated SDK artifact source; `tools/windows/fetch-v8.ps1` downloads it into
`.sdeps/v8/_downloads`, extracts `.sdeps/v8/windows-x64`, and validates the SDK layout
before it can be used. `tools/windows/resolve-v8-sdk.ps1 -Fetch` and
`tools/windows/dev.ps1 configure -EnableV8` use that source when no compatible local SDK
is found. Linux x64 uses the same ownership model through `tools/unix/build-v8.sh`, which
builds the pinned V8 revision, packages `.sdeps/v8/linux-x64`, and writes a reusable SDK
archive under `artifacts/v8-sdk/`; it does not adapt distro Node/V8 development packages.
The Linux SDK is built with V8's Chromium libc++ support and records ABI flags from V8's
generated feature metadata. macOS SDK artifacts remain planned.
`SLOPPY_V8_ROOT` is an advanced override, not the happy path. Rust dependencies are owned
by the `compiler/` project. JavaScript dependencies are not a package-manager surface for
Sloppy apps.

## Packages

Current package artifacts are experimental evidence for layout and outside-checkout
behavior. GitHub Release archives are the canonical distribution artifacts. The current
local alpha archive name is `sloppy-windows-x64.zip`. Linux x64, macOS arm64, and macOS x64
archive names are planned/optional lanes and must be reported only when that lane is
requested and evidenced. The alpha layout is:

```text
sloppy-<platform>-<arch>/
  bin/
  stdlib/
    sloppy/
  examples/
  docs/
    KNOWN_LIMITATIONS.md
    LICENSES.md
    NOTICE.md
  manifest.json
```

They are not a public release, installer, hosted distribution, or compatibility claim.
Package fixtures must not recompile source when the package contract says execution comes
from packaged artifacts. Default package smoke proves packaged CLI startup, `sloppy doctor`,
manifest/checksum layout, required docs, stdlib/examples presence, and no accidental source
checkout dependency; V8 package execution remains a separate V8-gated lane.
Linux V8 package smoke must use `tools/unix/dev.sh package --enable-v8` followed by
`tools/unix/dev.sh test-package --require-v8-runtime` and prove extracted-package JS app
execution from both compiled artifacts and source input before it is reported as runtime
user evidence.

## CI

CI separates default non-V8, static, Rust, package, optional V8, and heavier lanes. Skipped
or unavailable optional lanes are not pass evidence. Required checks must be green and
current before merge.

Manual artifact dry-runs use the `release-artifacts` GitHub Actions workflow or the local
`tools/<platform>/release-dry-run` scripts. The workflow is `workflow_dispatch` only,
uses read-only repository permissions, restores the same Rust/vcpkg caches as CI, uploads
package archives with `SHA256SUMS.txt`, and does not create a public GitHub release.

Default pull-request CI stays focused on static checks and fast tooling lanes. Package
smoke CI is opt-in through `workflow_dispatch`, `full-ci`, or `package-smoke` because it
performs an outside-checkout package build/smoke and is intentionally heavier than the
fast path. PRs that skip remote package smoke must report the skipped lane separately and
provide local package evidence when package behavior changes.

Release notes and limitation templates live in `RELEASE_NOTES.md`, `CHANGELOG.md`, and
`docs/release/`. They must keep unsupported platforms, V8 status, provider status, and
package smoke evidence separate. Skipped or unavailable lanes are not pass evidence.

The npm package track is a dry-run convenience installer for Sloppy itself. `@sloppy/runtime`
is a launcher that resolves a generated platform package such as
`@sloppy/runtime-win32-x64` or `@sloppy/runtime-linux-x64-gnu`. Platform package contents
must be generated from already-built archive contents. npm install must not compile native
code, run `node-gyp`, build V8, or download V8. This does not add npm dependency support to
Sloppy apps.

## Dogfood And Readiness

ALPHA-INFRA dogfood/test-app status is cataloged in
`examples/dogfood/alpha-dogfood.json`. The harnesses are:

```powershell
.\tools\windows\dogfood.ps1 -StatusOnly
.\tools\windows\dev.ps1 dogfood -Preset windows-relwithdebinfo -EnableV8
```

```sh
tools/unix/dogfood.sh
tools/unix/dev.sh dogfood
```

The catalog names the current hello artifact/source-input/package lanes and future HTTP,
HTTPS/TLS, SQLite, PostgreSQL, SQL Server, and Framework v2 feature apps. Blocked,
skipped, unavailable, V8-gated, package-gated, and live-provider-gated entries are not pass
evidence.

`docs/project/alpha-infra-readiness.json` is the machine-readable ALPHA-INFRA input for
#300 and related packaging/release/final-gate issues. It is internal gate evidence, not
final public documentation or a public release claim.

## Non-Claims

Sloppy does not currently claim production-ready builds, public alpha release artifacts,
TLS distribution, npm app dependency support, package-manager integration, or Node/Bun/Deno
compatibility. It makes no benchmarked performance result claim.
