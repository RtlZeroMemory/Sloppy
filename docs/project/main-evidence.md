# MAIN Evidence And Gate Report

Status: ROADMAP MAIN evidence/reporting contract.

MAIN evidence must say exactly which gate ran and what that gate proves. Default non-V8
success is useful, but it is not proof of V8 runtime execution, live database behavior,
package release readiness, benchmark performance, or public alpha readiness.

## Default Gates

The default Windows evidence set is:

```powershell
.\tools\windows\bootstrap.ps1
.\tools\windows\dev.ps1 configure
.\tools\windows\dev.ps1 build
.\tools\windows\dev.ps1 test
.\tools\windows\dev.ps1 format-check
.\tools\windows\dev.ps1 lint
.\tools\windows\check-js-ts-standards.ps1
.\tools\windows\check-rust-standards.ps1
cargo fmt --manifest-path compiler/Cargo.toml -- --check
cargo clippy --manifest-path compiler/Cargo.toml -- -D warnings
cargo test --manifest-path compiler/Cargo.toml
git diff --check
```

These commands prove:

- the default build configures, compiles, and runs its registered CTest suite;
- default CTest covers portable runtime foundations, compiler artifact determinism,
  default CLI/process diagnostics, default provider diagnostics, static/bootstrap checks,
  MAIN1-13 non-V8 conformance checks, and benchmark harness smoke where registered;
- format, lint, artifact hygiene, platform/C standards, docs freshness, JS/TS standards,
  and Rust standards checks passed;
- Rust compiler formatting, clippy, and tests passed;
- the default path is non-V8 unless V8 is explicitly enabled.

These commands do not prove:

- V8 runtime execution or the MAIN hello handler running through V8;
- live PostgreSQL or SQL Server provider behavior;
- package installability outside the checkout unless package smoke also ran;
- package release readiness, signing, notarization, package-manager integration, or public
  distribution readiness;
- benchmark/performance claims;
- public alpha readiness.

When reporting default gate results, call them default non-V8 results. Do not collapse them
into "MAIN passed" if optional V8, package, provider, or benchmark evidence was skipped.

## Compiler Artifact Smoke

The MAIN artifact smoke builds the canonical supported compiler source shape:

```powershell
cargo run --manifest-path compiler/Cargo.toml -- build examples/compiler-hello/app.js --out .sloppy-main-smoke
```

When an installed `sloppyc` binary from the relevant build/package is the subject under
test, the equivalent command is:

```powershell
sloppyc build examples/compiler-hello/app.js --out .sloppy-main-smoke
```

The output directory must contain `app.plan.json`, `app.js`, and `app.js.map`. Repeated
builds must produce byte-identical artifact contents with stable handler IDs and without
absolute local paths, timestamps, random IDs, or checkout-specific paths.

This proves deterministic compiler emission for the canonical supported source shape. It
does not prove runtime execution, source-input `sloppy run`, broad TypeScript extraction,
Node/npm resolution, package release readiness, or public alpha readiness.

## V8-Gated Evidence

V8 evidence exists only when the build is explicitly configured with an approved SDK.
Default gates and required hosted CI leave V8 disabled.

Validate the SDK when a local SDK root is available:

```powershell
.\tools\windows\resolve-v8-sdk.ps1
.\tools\windows\fetch-v8.ps1 -ValidateOnly
```

Configure, build, and test the V8-enabled preset:

```powershell
.\tools\windows\dev.ps1 configure -Preset windows-relwithdebinfo -EnableV8
.\tools\windows\dev.ps1 build -Preset windows-relwithdebinfo
.\tools\windows\dev.ps1 test -Preset windows-relwithdebinfo
```

The equivalent CMake selection is `SLOPPY_ENABLE_V8=ON` or `SLOPPY_ENGINE=v8` with
`SLOPPY_V8_ROOT=<sdk-root>`. The Windows wrapper resolves that value from `-V8Root`,
`SLOPPY_V8_ROOT`, `SLOPPY_V8_SDK_HINTS`, and registered worktree `.sdeps` locations. The
current Windows source SDK is release/RelWithDebInfo compatible; do not use it with the
default Debug `windows-dev` preset.

Run the MAIN hello run-once smoke only from the V8-enabled build after creating the
compiler smoke artifacts:

```powershell
.\build\windows-relwithdebinfo\sloppy.exe run --artifacts .sloppy-main-smoke --once GET /
```

The expected response body for the canonical fixture is:

```text
Hello from Sloppy
```

Successful V8 evidence proves the configured SDK was accepted, the V8-enabled build/test
suite ran, and the supported compiler artifact path can execute the MAIN hello handler
through the current classic bootstrap/runtime-contract path.
MAIN1-13 V8-gated CTest conformance also proves the request-context example executes
through the same artifact boundary, supported `Results.text` and `Results.json` descriptors
serialize through the dev response path, invalid result descriptors fail safely, and the
SQLite bridge fixture can run an in-memory create/insert/select/close flow.

It does not prove true V8 ESM bootstrap module loading, Node/npm compatibility, arbitrary
imports, source-input `sloppy run`, production HTTP serving, provider bridge behavior
beyond the checked-in SQLite conformance fixture, package release readiness, dynamic V8
runtime packaging, or public alpha readiness.

If the SDK is unavailable, report V8 evidence as skipped or unavailable. Do not infer V8
success from default gates.

## Package Evidence

The current Windows package command is:

```powershell
.\tools\windows\package.ps1 -Configuration Release -Smoke
```

The explicit outside-checkout smoke command for an existing archive is:

```powershell
.\tools\windows\test-package.ps1 -PackagePath artifacts\packages\sloppy-0.0.0-dev-windows-x64.zip
.\tools\windows\dev.ps1 test-package
```

Package smoke extracts the archive under a temporary directory outside the checkout, runs
`sloppy --version`, `sloppy --help`, `sloppy doctor`, `sloppyc --version`, and
`sloppyc --help`, verifies stdlib assets, examples, package docs and manifest fields,
checks excluded local/build directories and V8 SDK files are absent, and verifies
`SHA256SUMS.txt` when present.

Package smoke proves the local Windows archive layout can start the basic packaged CLI
tools outside the source checkout. It does not prove V8 runtime execution, live providers,
source-input run behavior, installers, signing/notarization, package-manager distribution,
auto-update, reproducible release builds, or public release readiness.

Linux/macOS packaging currently has an experimental TAR staging script:

```sh
tools/unix/package.sh --configuration Release
tools/unix/test-package.sh --package-path artifacts/packages/sloppy-0.0.0-dev-<platform>-<arch>.tar.gz
tools/unix/dev.sh package
tools/unix/dev.sh test-package
```

Hosted Linux/macOS default CI validates non-V8 configure/build/test, Cargo gates, and POSIX
standards scanners. It does not yet run package execution smoke for Linux/macOS archives.

V8 runtime packaging is not proven by default package smoke. The default local package is
non-V8 and records no V8 SDK inclusion. Any V8 runtime packaging evidence must state the
build, SDK source, package flags, require-V8 package smoke, and V8-gated package execution
that actually ran. Runtime-file presence alone is not V8 execution evidence. Linux x64 V8
packages use the Sloppy-owned SDK from `tools/unix/build-v8.sh`; static SDK packages may
link V8 into `bin/sloppy` without separate runtime files, so the proof is extracted
package JS execution from both artifacts and source input.

Current local Docker evidence for the Linux x64 V8 package lane:

```sh
tools/unix/build-v8.sh --package-only --work-root /tmp/v8-proper-work --sdk-root /work/.sdeps/v8/linux-x64 --archive-dir /work/artifacts/v8-sdk
tools/unix/dev.sh package --enable-v8
tools/unix/dev.sh test-package --require-v8-runtime
```

Evidence result: PASS in `sloppy-linux-v8-runtime-final-808584149` on May 8, 2026.
The SDK was rebuilt in Docker from the pinned V8 checkout with the same work root before
this final package-only repackaging pass; the final passing container did not fetch or
rebuild V8 again after the bridge stack-size fix because the SDK bits were unchanged.
The extracted package ran both V8-backed artifact execution and V8-backed source-input
execution, each returning `HTTP/1.1 200 OK` with `Hello from packaged Sloppy`.

## Dogfood And Alpha Infra Evidence

ALPHA-INFRA dogfood evidence is cataloged in `examples/dogfood/alpha-dogfood.json` and
reported through:

```powershell
.\tools\windows\dogfood.ps1 -StatusOnly
.\tools\windows\dev.ps1 dogfood -Preset windows-relwithdebinfo -EnableV8
```

The status-only lane validates the dogfood catalog and reports blocked/unavailable feature
apps. It does not prove runtime execution. Positive hello execution requires the
V8-enabled source-input/artifact lane. Package-mode dogfood requires an explicit package
archive and must stay separate from default source-input evidence.

`docs/project/alpha-infra-readiness.json` is the machine-readable input for #300. It records
ALPHA-INFRA issue completion, deferred #876 V8 artifact hosting, platform/package/V8 lane
status, release dry-run status, and dogfood status. This is internal gate evidence, not a
public alpha or final release verification.

## Live Provider Evidence

SQLite default evidence is in-memory native provider coverage through the default CTest
suite. This is useful SQLite provider evidence, but it is not JavaScript-to-native bridge
evidence and it is not a live external service check.

PostgreSQL live provider coverage is opt-in:

```powershell
$env:SLOPPY_POSTGRES_TEST_URL="<redacted PostgreSQL connection string>"
.\tools\windows\dev.ps1 test
```

SQL Server live provider coverage is opt-in and also depends on a local ODBC driver and a
reachable server:

```powershell
$env:SLOPPY_SQLSERVER_TEST_CONNECTION_STRING="<redacted SQL Server connection string>"
.\tools\windows\dev.ps1 test
```

Default provider tests prove non-live provider diagnostics, redaction behavior, option
validation, and unavailable/stub behavior where configured. They do not prove live
PostgreSQL or SQL Server connectivity unless the relevant environment variable was set and
the run reported that the live gate executed.

Do not paste secrets into PR bodies, logs, fixtures, or docs. Reports should name the
environment variable and use redacted placeholders.

## Benchmark Evidence

Benchmark smoke/list commands are:

```powershell
.\tools\windows\bench.ps1 -List
.\tools\windows\bench.ps1 -Smoke -Json
```

Measured local benchmark commands use a Release build:

```powershell
.\tools\windows\bench.ps1 -Configuration Release
.\tools\windows\bench.ps1 -Configuration Release -Json > .\benchmarks-local.json
```

List/smoke checks prove the benchmark harness starts, exposes expected benchmark names, and
can execute tiny smoke iterations. They are harness correctness checks, not performance
claims.

Current measured scenarios are foundation microbenchmarks for route parsing/matching,
complete-buffer HTTP request-head parsing, handler plan lookup/noop dispatch plumbing, and
synthetic dispatch paths. V8 handler timing, real HTTP throughput, JSON serialization,
live database benchmarks, trend gates, dashboards, and external runtime comparisons remain
deferred unless a future scoped task adds methodology and evidence.

Do not use benchmark smoke output, Debug numbers, or one-off local numbers as public
performance claims.

## Reporting Template

Evidence reports and PR bodies should separate:

- default non-V8 gates run or skipped;
- V8-gated commands run or skipped;
- default package smoke run or skipped;
- V8 package smoke run or skipped;
- sanitizer/fuzz gates run or skipped;
- live provider gates run or skipped;
- benchmark checks run or skipped;
- non-goals, especially runtime/compiler/provider implementation changes.
