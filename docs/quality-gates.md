# Quality Gates

## Purpose

Quality gates define the minimum checks required before code can be trusted. They keep
foundation work from becoming a pile of unverified intentions.

## Scope

This document covers local gates, CI gates, required tools, local warnings versus CI
failures, artifact hygiene, platform-boundary scanning, C/Rust formatting and linting,
CTest, future sanitizer/fuzz gates, exact commands, and acceptance criteria.

## Non-Goals

This document does not replace detailed testing strategy in `docs/testing.md` or the
testing philosophy in `docs/testing-strategy.md`.

## Current Phase

Current gates cover C/Rust builds, formatting, linting, CTest, cargo tests, compiler
goldens, artifact hygiene, platform-boundary scanning, C standards scanning, JS/TS
standards scanning, Rust standards scanning, and a lightweight docs freshness structure
check.
Default CI now runs Windows clang-cl, Linux clang, Linux gcc, and macOS clang non-V8 gates.
Benchmark list/smoke checks may run as correctness smoke, but performance deltas are not a
normal hard gate yet.

Default gate success must be reported as default non-V8 evidence. It does not prove V8
runtime execution, live PostgreSQL or SQL Server behavior, package release readiness,
benchmark/performance claims, or public alpha readiness. `docs/project/main-evidence.md`
is the source document for reporting those evidence categories separately.
MAIN1-13 conformance follows the same split: default conformance compiles supported public
examples and rejects unsupported inputs, while V8-gated conformance runs compiled artifacts
and selected bridge/result fixtures through `sloppy run --artifacts --once`.
ENGINE-13.F HTTP stress/conformance smoke is default non-V8 correctness evidence over the
core backend/parser/dispatch state model. It is reported separately from V8-gated
`sloppy run` execution, live-provider evidence, package smoke, and benchmark evidence. It
does not introduce throughput, latency, external-runtime comparison, or production-edge HTTP
claims.
ENGINE-24.F localhost transport smoke is default non-V8 correctness evidence over the
libuv-backed loopback TCP transport MVP. It proves bounded local request/response bytes,
one-request-per-connection, close-after-response, Content-Length-only bodies, deterministic
failure responses, and cleanup coherence. It is still separate from V8 transport execution,
benchmark/performance evidence, keep-alive/pipelining/streaming support, live-provider
evidence, and production-edge HTTP readiness.

## Future Phase

Future gates add sanitizers, fuzzing, richer diagnostics snapshots, benchmarks,
docs link checking, public example tests, broader package verification, and optional live
provider service jobs.

## Public API Shape

The gate API is the developer script interface under `tools/windows/dev.ps1` plus root
forwarders.

## Required Tools

Current Windows workflow requires:

- Git;
- CMake;
- Ninja;
- `clang-cl`;
- `lld-link`;
- `clang-format`;
- `clang-tidy`;
- Rust/Cargo;
- rustfmt;
- clippy.

The shell must expose MSVC and Windows SDK include/lib paths for build commands.

## Local Commands

Root wrappers:

```powershell
.\tools\bootstrap.ps1
.\tools\dev.ps1 configure
.\tools\dev.ps1 build
.\tools\dev.ps1 test
.\tools\dev.ps1 format-check
.\tools\dev.ps1 lint
```

Explicit Windows path:

```powershell
.\tools\windows\bootstrap.ps1
.\tools\windows\dev.ps1 configure
.\tools\windows\dev.ps1 build
.\tools\windows\dev.ps1 test
.\tools\windows\dev.ps1 format-check
.\tools\windows\dev.ps1 lint
```

Rust direct gates:

```powershell
cargo fmt --manifest-path compiler/Cargo.toml -- --check
cargo clippy --manifest-path compiler/Cargo.toml -- -D warnings
cargo test --manifest-path compiler/Cargo.toml
```

Language standards scanners:

```powershell
.\tools\windows\check-js-ts-standards.ps1
.\tools\windows\check-rust-standards.ps1
```

Benchmark workflow for performance-validation tasks:

```powershell
.\tools\windows\bench.ps1 -List
.\tools\windows\bench.ps1 -Smoke -Json
.\tools\windows\bench.ps1 -Configuration Release
```

Do not report Debug or smoke output as meaningful performance data.

Packaging workflow for EPIC-25 and later package changes:

```powershell
.\tools\windows\package.ps1 -Configuration Release
.\tools\windows\test-package.ps1 -PackagePath artifacts\packages\sloppy-0.0.0-dev-windows-x64.zip
```

Unix local package smoke:

```sh
tools/unix/package.sh --configuration Release
tools/unix/test-package.sh --package-path artifacts/packages/sloppy-0.0.0-dev-<platform>-<arch>.tar.gz
```

The package smoke must extract outside the checkout, run basic CLI commands, verify stdlib
assets and manifest fields, and verify the checksum file. It is a local artifact smoke, not
a reproducible public-release claim.

The package command may also run smoke directly:

```powershell
.\tools\windows\package.ps1 -Configuration Release -Smoke
```

That smoke proves the local Windows archive layout starts basic packaged CLI commands
outside the checkout. It does not prove V8 runtime packaging, live providers, installers,
signing/notarization, package-manager distribution, or public alpha release readiness.

V8 runtime package validation is optional and separate:

```powershell
.\tools\windows\package.ps1 -Configuration RelWithDebInfo -IncludeV8Runtime -Smoke
.\tools\windows\test-package.ps1 -PackagePath <zip> -RequireV8Runtime
```

Those checks validate manifest/runtime-file layout only. A V8 package execution claim also
requires a V8-enabled `sloppy run --artifacts ... --stdlib <extracted-package>/lib/sloppy/
bootstrap/sloppy --once GET /` smoke from the extracted package. Default package smoke is
non-V8 unless these V8-specific checks ran and passed.

## Local Versus CI Behavior

Locally, missing optional quality tools may print a clear skip warning. In CI, missing
required quality tools fail the job.

Warnings may be informational locally while the project is early. CI should enable
warnings-as-errors for configured build and lint gates.

Local warnings that may be skips today:

- missing `clang-format`;
- missing `clang-tidy`;
- missing `rustfmt`;
- missing `clippy`.

CI behavior:

- missing any required quality tool fails;
- skipped platform/provider integration tests must state the missing environment;
- warnings-as-errors is enabled for the C build;
- clippy warnings are errors.

## CI Gates

Default CI runs a required non-V8 matrix:

- Windows clang-cl through `windows-dev`;
- Linux clang through `linux-clang`;
- Linux gcc through `linux-gcc`;
- macOS clang through `macos-clang`.

Each required job should run:

- checkout;
- platform toolchain setup;
- Rust setup;
- vcpkg bootstrap/restore;
- generated artifact tracking check;
- CMake configure with `SLOPPY_ENABLE_WERROR=ON`;
- CMake build;
- CTest;
- Cargo build;
- cargo fmt;
- cargo clippy;
- cargo test;
- platform/C standards scanners appropriate to the runner.

The Windows job remains the full local-gate mirror and runs `tools/windows/dev.ps1`
`format-check` and `lint`, including C, JS/TS, Rust, docs freshness, and artifact-hygiene
scanners. Linux/macOS jobs run direct CMake/Cargo commands plus POSIX platform and C
standards scanners. They do not require PowerShell-only Windows wrapper behavior.

Optional/gated jobs:

- V8 validation is manual through `workflow_dispatch`. It requires an explicit
  `enable_v8=true` input and a runner-local `v8_root` path. If no SDK path is supplied or
  the path is absent, the job reports that V8 validation was skipped/not configured and
  does not pretend to run V8 tests. When configured, it validates the SDK, configures
  `windows-relwithdebinfo` with `SLOPPY_ENABLE_V8=ON`, builds, and runs the V8-enabled
  CTest suite.
- Live PostgreSQL and SQL Server provider tests remain opt-in through
  `SLOPPY_POSTGRES_TEST_URL` and `SLOPPY_SQLSERVER_TEST_CONNECTION_STRING`. Default CI
  prints provider gate status and runs non-live provider tests; it does not require live
  services or secrets.
- Live provider CTests must be registered as separate optional gates and use skip code `77`
  for not-configured environments. A skipped live provider test is a reported skip, not a
  pass claim.
- Package smoke is not part of the default required CI matrix. A PR that reports package
  evidence must list the package command and outside-checkout smoke result separately from
  default CI.
- Linux/macOS package smoke has local tooling but is not required in hosted CI until a
  scoped package-smoke job is added.
- Benchmark list/smoke checks are harness checks. Measured Release benchmark runs, when
  scoped, must be reported separately and must not be described as public performance
  claims.

## Documentation Freshness

Docs freshness is a quality gate. A PR that changes behavior, APIs, module boundaries,
diagnostics, architecture, CLI behavior, test expectations, or public examples must update
the relevant docs or explicitly explain why docs did not change.

The lightweight `tools/windows/check-docs-freshness.ps1` gate verifies required policy
docs, public docs, and module docs skeletons exist and that module docs contain required
headings. Stronger semantic checks should be added only when they are reliable enough to
avoid noisy false failures.

Testing follows the tests-as-intent rule from `docs/testing-strategy.md`: tests verify
documented intended behavior, not accidental current behavior.

## Simplicity Review

Simplicity is reviewed manually for now. Reviewers should use `docs/c-standards.md` to
reject speculative abstraction, framework-like subsystems, and "safe-looking" code that
hides ownership, bounds checks, or error paths. JS/TS reviewers should use
`docs/js-ts-standards.md`; Rust compiler/tooling reviewers should use
`docs/rust-standards.md`.

Future optional complexity checks may warn on:

- very large functions;
- high nesting;
- too many parameters;
- macro-heavy code;
- one-call-site abstraction proliferation.

The warning-only `tools/windows/check-c-complexity.ps1` scanner is informational. Human
review decides whether complexity is justified.

## Comment Quality

Comment quality is primarily review-enforced. Public APIs and tricky internals need useful
comments for ownership, lifetime, invariants, and non-obvious safety, platform, engine, or
threading assumptions. Comments that narrate obvious syntax should be removed or replaced
with clearer code.

A future optional scanner may warn on low-value comment patterns such as "increment by
one", "set variable", or "return result", but humans decide context.

## Artifact Hygiene

Never stage or track:

- `artifacts/`;
- `build/`;
- `compiler/target/`;
- `target/`;
- `.sdeps/`;
- `.sloppy/`;
- `vcpkg_installed/`;
- `*.zip`;
- `*.7z`;
- `*.tar.gz`;
- `*.exe`;
- `*.pdb`;
- `compile_commands.json`.

CI checks tracked generated artifacts with `git ls-files`.

The lint gate should also reject staged ignored/generated artifacts before review. It must
not delete user files.

## Internal Architecture

`tools/windows/dev.ps1` is the gate orchestrator. CMake owns C build/test targets. Cargo
owns Rust build/test/lint. CI calls the same script path where practical.

## Platform-Boundary Scanner

`tools/windows/check-platform-boundaries.ps1` scans `include/` and `src/` for forbidden OS
headers outside platform implementation directories.

Forbidden outside allowed platform dirs:

- `<windows.h>`;
- `<winsock2.h>`;
- `<io.h>`;
- `<unistd.h>`;
- `<pthread.h>`;
- `<sys/epoll.h>`;
- `<sys/event.h>`.

The scanner runs from `tools/windows/dev.ps1 lint`.

Near-term hardening task: add scanner fixtures or a self-test mode so CI proves the scanner
detects a forbidden include outside `src/platform/*`.

## C Standards Scanner

`tools/windows/check-c-standards.ps1` scans C/C++ source and header files under `include/`
and `src/`. It prefers git-tracked files to avoid build artifacts, with a recursive fallback
for early untracked worktrees.

The scanner fails on:

- forbidden OS headers outside `src/platform/*`;
- unsafe or primitive-bypassing C functions such as `gets`, `strcpy`, `strcat`, `sprintf`,
  `vsprintf`, `snprintf`, and internal `strlen` outside approved boundary helpers;
- raw byte-copy helpers such as `memcpy`/`memmove` outside narrow approved boundary helpers,
  where high-level Slop string/byte/builder helpers should be preferred;
- V8 headers or `v8::` usage outside `src/engine/v8/*`.

It warns on raw `malloc`, `free`, `realloc`, and `calloc` outside allocator paths. Passing
`-StrictAlloc` turns those allocation warnings into failures. This mode is expected to
become more useful after allocator modules exist.

The scanner runs from `tools/windows/dev.ps1 lint`, so CI executes it through the lint step.
Local failures should be fixed or documented before review. POSIX CI also runs
`tools/unix/check-c-standards.sh` so Linux/macOS can enforce OS-header and V8-boundary
rules without routing through the Windows PowerShell wrapper.

## C/C++ Formatting And Linting

`clang-format` checks C/C++ sources and headers.

`clang-tidy` runs against configured compile commands and treats warnings as errors in the
script/CMake targets.

## Language Standards Source Docs

Prompt and PR preparation must include the right language docs:

- stdlib, public JS/TS API, compiler fixture, or example PRs must read
  `docs/js-ts-standards.md`;
- compiler/tooling PRs must read `docs/rust-standards.md`;
- runtime C/C++ PRs must read `docs/c-standards.md` and `docs/c-style.md`;
- every language follows `docs/testing-strategy.md`, `docs/documentation-policy.md`, and
  `docs/review-playbook.md`.

## JS/TS Standards Scanner

`tools/windows/check-js-ts-standards.ps1` scans source-controlled JS/TS stdlib, examples,
compiler fixtures, and golden/fixture inputs without requiring Node or npm.

The scanner fails on obvious policy violations:

- Node globals/imports in `stdlib/sloppy/`;
- CommonJS usage;
- dynamic imports;
- package-manager files under `examples/`;
- obvious top-level stdlib side effects;
- conservative secret-looking assignments in examples/fixtures, with redacted placeholder
  values allowed.

This scanner is a lint gate and does not replace behavior tests. It deliberately starts as
a zero-dependency structural check; a future parser-based JS linter may replace or
supplement it after the compiler/tooling story is scoped.

## Rust Standards Scanner

`tools/windows/check-rust-standards.ps1` scans production Rust under `compiler/src/` for
project-specific compiler/tooling rules that cargo fmt and clippy do not express.

The scanner fails on:

- `unwrap()` and `expect()` in production code without an explicit allow comment and reason;
- `todo!()`, `unimplemented!()`, `panic!()`, and `dbg!()` in production code;
- `println!()` / `eprintln!()` outside the current CLI entrypoint unless explicitly
  allowed;
- `HashMap` / `HashSet` in obvious artifact/diagnostics paths without a deterministic
  ordering reason;
- absolute local paths in future compiler golden outputs.

Tests may use `unwrap()` and `expect()` where appropriate. The current scanner skips
`#[cfg(test)] mod tests` blocks in `compiler/src/*`.

## Rust Formatting, Linting, Tests

`cargo fmt --check`, `cargo clippy -- -D warnings`, and `cargo test` are required for
compiler changes and run as part of quality gates. The Rust standards scanner complements
these gates; it is not a replacement for cargo fmt, clippy, or tests.

## CMake And CTest

CMake must configure and build with the Windows preset. CTest must pass and include smoke
coverage for both `sloppy` and `sloppyc` while the project is in placeholder phase.

V8 remains an opt-in build gate. Default and CI builds leave the V8 bridge disabled and do
not require a V8 SDK. Local Windows V8 gates should start with:

```powershell
.\tools\windows\resolve-v8-sdk.ps1
.\tools\windows\dev.ps1 configure -Preset windows-relwithdebinfo -EnableV8
.\tools\windows\dev.ps1 build -Preset windows-relwithdebinfo
.\tools\windows\dev.ps1 test -Preset windows-relwithdebinfo
```

The resolver searches `-V8Root`, `SLOPPY_V8_ROOT`, `SLOPPY_V8_SDK_HINTS`, this worktree's
`.sdeps/v8/windows-x64`, and the same path in registered git worktrees. Direct CMake users
must pass `-DSLOPPY_V8_ROOT=<sdk-root>` explicitly. A manual V8-enabled configure must fail
clearly when the resolved SDK root is empty or does not contain the documented SDK layout.

When the V8 SDK gate succeeds, the V8 bridge source, `engine.v8.smoke`,
`engine.v8.owner_thread`, V8-gated bootstrap runtime tests, and V8-gated `sloppy run`
process tests are compiled and linked only in that V8-enabled build. Passing default CTest
does not prove V8 smoke, owner-thread, lifecycle, Promise-policy, or bootstrap runtime
tests passed; the V8-enabled configure/build/test commands must be run and reported
separately.

Phase 1 CTest expectations:

- `core.status.*`;
- `core.source_loc.*`;
- `core.str.*`;
- `core.bytes.*`;
- `core.checked_math.*`;
- `core.scope.*`;
- existing CLI smoke remains;
- CLI introspection output changes include deterministic process-level golden tests over
  metadata fixtures.

CLI introspection tests must not require handler execution, HTTP server startup, V8
execution, or live database servers by default. Live provider checks must stay behind
explicit future flags or environment gates.

## Future Gates

- ASan/UBSan builds. Start with non-V8 core/default builds on platforms where the compiler
  and dependency stack support stable sanitizer output. Windows `windows-asan` is an
  available local preset but is not yet a required hosted CI gate. UBSan is expected to
  start on clang/gcc before becoming required.
- fuzz tests. First candidates are route pattern parsing, HTTP request-head parsing, Plan
  JSON parsing, diagnostics/source map parsing once richer source maps exist, and resource
  ID/table validation. Fuzz targets should be short-running, deterministic, and isolated
  from sockets, live providers, V8, package managers, or network access by default.
- diagnostics snapshot tests;
- compiler golden tests;
- benchmark trend checks;
- packaging smoke tests;
- optional live PostgreSQL/SQL Server service jobs;
- V8 SDK cache or prebuilt artifact setup for the manual V8 job.

Future sanitizer/fuzz gates should begin as opt-in local commands, then become CI jobs once
they are stable enough not to create noisy false failures. Until then, default CI success
must not be reported as sanitizer or fuzz coverage.

## Development Tasks

- Add CMake targets for future test suites.
- Add sanitizer presets to CI when stable.
- Add fuzz job when first fuzz target exists.
- Add diagnostics/golden artifact checks.
- Add Linux/macOS quality gate variants when platform support exists.

## Acceptance Criteria

Quality gates are acceptable when:

- commands are documented;
- local skips are explicit;
- CI fails on missing required tools;
- generated artifacts are not tracked;
- platform-boundary violations fail lint;
- C standards violations fail lint;
- JS/TS standards violations fail lint;
- Rust standards violations fail lint;
- docs freshness structure checks pass;
- Rust and C gates both run;
- future gates have clear placeholders.

For Phase 1 implementation PRs:

- the PR maps to a roadmap EPIC/task;
- all touched C files are clang-formatted;
- new C primitives have CTest coverage;
- docs/specs update if behavior changes;
- user-facing docs/module docs update when public behavior or module behavior changes;
- tests cite or encode documented intended behavior;
- lint includes platform-boundary scan;
- lint includes C standards scan;
- cargo gates pass when compiler files changed.

## Open Questions

- Which CI runner gets sanitizer gates first.
- Whether clang-tidy should lint every C file through a generated target.
- Exact benchmark regression threshold once benchmarks exist.
