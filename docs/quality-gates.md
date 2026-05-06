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

Post-ENGINE-16 Roadmap-2 gate policy: this phase is planning/consolidation before new
runtime maturation implementation. Default gate success remains default non-V8 evidence.
ENGINE-26 through ENGINE-30 work must report execution-model, feature-modularity,
provider, HTTP policy, events/metrics, torture/stress, V8-gated, live-provider, package,
and benchmark evidence separately. No Roadmap-2 task may turn skipped optional lanes into
success, public alpha readiness, or benchmark/performance claims.

HTTP-25.A/B/C update: `core.http.transport` and
`conformance.transport.localhost_mvp` now cover bounded sequential keep-alive behavior in
the default non-V8 lane. Passing those tests proves local HTTP/1.1 sequential reuse, idle
timeout, max requests, close policy, lifecycle reset, and deterministic pipelining
rejection. HTTP-25.D/E extends that lane with bounded chunked request decoding and
internal/native chunked response writer correctness. It does not prove V8 transport
execution, public streaming helpers, SSE/WebSockets/file streaming, production graceful
drain, benchmark/performance behavior, TLS, HTTP/2/3, or WebSockets.
HTTP-25.F adds named default non-V8 conformance aliases for keep-alive, idle timeout, max
requests, lifecycle reset, chunked requests, streaming responses, backpressure, and
shutdown/cancel behavior, plus `smoke.transport.keep_alive_streaming_bounded` for repeated
bounded transport operations. This is stress/smoke evidence only; it does not register
benchmark evidence or production-edge HTTP evidence.
ENGINE-27 adds default-lane feature registry and missing-feature diagnostic tests. Passing
them proves deterministic Plan-driven feature activation, unknown/unavailable/dependency
runtime-feature diagnostics, and renderer-pinned missing-feature output only; it does not
prove package trimming, provider expansion, or dynamic feature loading.
CORE-FS-01 gates must report filesystem evidence by lane. PRs that only add the `stdlib.fs`
contract and Plan metadata do not prove filesystem I/O, V8 bridge execution, OS-native watch
behavior, stream behavior, package readiness, or performance.
CORE-FS-01.I/J adds filesystem doctor/audit goldens for capability visibility and source
examples for the implemented API; those gates do not prove OS sandboxing or public alpha
readiness.
CORE-TIME-01.I adds `examples.time.api_shape`, final Time diagnostic goldens, and the
Time conformance evidence index. These gates prove source examples and deterministic
default-lane diagnostics/bootstrap behavior only; V8 Time owner-thread evidence remains in
the V8-gated lane, and no Node timer compatibility, global fake timers, cron parser,
package-manager behavior, public alpha readiness, or benchmark claims are implied.
CORE-CRYPTO-01 gates must report crypto evidence by lane. PRs that add random/hash/HMAC/
Secret/V8 support prove OS-source use, API shape, standard SHA-2/HMAC vectors, cleanup
lifecycle, and V8 bridge registration only. They do not prove random quality, password
cost, side-channel resistance, package readiness, or performance. CORE-CRYPTO-01.I adds
`examples.crypto.api_shape` and `tests/conformance/crypto/README.md`; those gates prove
source example shape and evidence indexing only. Secure `Hash`/`Hmac` evidence remains
separate from explicitly non-security `NonCryptoHash` evidence.
CORE-NET-01 gates must report network evidence by lane. PRs that only add the `stdlib.net`
contract, Plan metadata, diagnostics, and policy model do not prove TCP execution, external
network access, V8 execution, package readiness, throughput, or performance. Default tests
must remain deterministic localhost/loopback only once TCP behavior lands; live-network and
benchmark lanes stay optional and separately reported. CORE-NET-01.I adds
`examples.net.api_shape`, `sloppy.cli.doctor_network_*`, `sloppy.cli.audit_network_*`, and
`tests/conformance/net/README.md`; those gates prove source shape, metadata visibility, and
evidence indexing only.
CORE-CODEC-01 gates must report codec evidence by lane. PRs that only add the
`stdlib.codec` contract, Plan metadata, diagnostics, and backend policy do not prove
Base64/Hex/Text/Binary/Compression/Checksum correctness, V8 execution, package readiness,
streaming/backpressure behavior, compression backend availability, or performance.
CORE-CODEC-01.F/G evidence is split between default JS compression-surface tests and
V8-gated zlib backend tests; it still does not prove Web Streams compatibility, broader
algorithms, package readiness, or performance. CORE-CODEC-01.H/J adds CRC32 vectors,
example checks, and checksum security-context warnings; this evidence remains separate from
`sloppy/crypto` security evidence.

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
libuv-backed loopback TCP transport MVP. HTTP-25.A/B/C extends that default localhost lane
with bounded sequential HTTP/1.1 keep-alive, `Connection: close`, HTTP/1.0 close policy,
idle timeout, max requests, request lifecycle reset, and deterministic pipelining
rejection. HTTP-25.D/E adds chunked request decoding, internal chunked response streaming,
final zero chunk emission, and pending-write cap coverage. It is still separate from V8
transport execution, benchmark/performance evidence, public streaming helpers,
SSE/WebSockets/file streaming, live-provider evidence, and production-edge HTTP readiness.
ENGINE-17.E users API localhost proof is V8-gated evidence. It builds the source fixture
with `sloppyc`, starts `sloppy run --artifacts`, sends real localhost TCP HTTP requests,
and verifies SQLite/capability/result/body behavior. Passing it does not prove default
non-V8 V8 execution, async/offload SQLite, PostgreSQL/SQL Server JS bridges, public alpha
readiness, benchmark performance, or production-edge HTTP behavior.
ENGINE-19.A adds the conformance evidence matrix in
`docs/project/engine-19-conformance-matrix.md`. That matrix owns the naming, skip, and
PR-reporting rules for default non-V8, V8-gated, localhost transport, SQLite/capability,
package outside-checkout, live-provider optional, stress/smoke, and benchmark harness
evidence. Skipped optional gates are not pass claims.
ENGINE-19.BC adds first-class CTest registrations for the implemented V8, HTTP, and async
evidence lanes: `conformance.http.default_dispatch`, `conformance.transport.localhost_mvp`,
HTTP-25.F's `conformance.transport.keep_alive*`,
`conformance.transport.lifecycle_reset`, `conformance.transport.chunked_request`,
`conformance.transport.streaming_response`, `conformance.transport.backpressure`,
`conformance.transport.shutdown_cancel`, `smoke.transport.keep_alive_streaming_bounded`,
`conformance.async.*`, and V8-gated `conformance.v8.*`. These entries run existing
validated executables under matrix-aligned names. Default gate success now includes the
HTTP-25.A/B/C sequential keep-alive smoke plus HTTP-25.D/E bounded chunked request decoding
and internal/native chunked response writer checks plus HTTP-25.F bounded stress smoke; it
still does not prove V8
execution, package, live-provider, benchmark, public streaming helpers, SSE/WebSockets/file
streaming, or production-edge HTTP behavior.
ENGINE-19.D adds first-class CTest registrations for the implemented SQLite and capability
evidence lanes: `conformance.sqlite.native_provider`,
`conformance.capability.native_registry`, `conformance.capability.provider_executor`,
V8-gated `conformance.sqlite.bridge`, `conformance.sqlite.denied_capability`, and
`conformance.users_api_sqlite.localhost_transport`. Default SQLite/capability evidence
still does not prove V8 execution, PostgreSQL/SQL Server JavaScript bridges,
live-provider behavior, package smoke, async SQLite offload, benchmark claims, public alpha
readiness, or production-edge HTTP behavior.
FRAMEWORK-01.B adds config-specific gate expectations: compiler tests cover precedence and
provider binding, bootstrap JS tests cover typed access and `bind`, source-input CTest
coverage verifies config-driven SQLite Plan output in the non-V8 lane, and docs/goldens
must show configuration metadata without claiming reload, secrets, custom providers,
Node/npm compatibility, or public alpha readiness.
ENGINE-16.D/E lifecycle evidence is default non-V8 unit evidence. Passing
`core.app_host.hardening`, `core.resource.lifecycle`, and `core.diagnostics.foundation`
proves deterministic lifecycle snapshots, resource-table snapshots, no-leak assertions,
late-completion counts, synthetic leak diagnostics, and stable lifecycle code names for the
native helper layer. It does not prove production monitoring, real timer/provider operation
counters, V8 execution, live providers, runtime torture, benchmark/performance behavior, or
public alpha readiness.

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

Example hardening gates include CTest compile-artifact coverage for the current example
set and `examples.*.tooling` checks that build artifacts and run Plan-driven
routes/doctor/audit/capabilities/OpenAPI commands over representative examples. V8-gated
example execution remains separate from the default lane.

For compiler-module changes, the Rust gates must cover both the CLI and the library API.
COMPILER-30.A adds fixture harness coverage for current artifacts, diagnostics, source-map
goldens, and staged generated-artifact hygiene. Passing those tests preserves current
compiler behavior; it does not prove future route/provider/capability inference.
COMPILER-30.H/I compiler changes must also prove strong Plan metadata and compatibility:
refreshed Plan goldens, complete/partial/runtime-only/invalid completeness unit tests,
missing-provider diagnostics, and native Plan parser coverage that accepts unknown optional
fields while rejecting unknown required features.
ENGINE-15.A compiler source-map changes must refresh deterministic `app.js.map` goldens,
verify source-input run emits Sloppy source-map metadata, and report that default non-V8
gates do not prove V8 exception remapping.
ENGINE-15.B V8 exception-remapping changes must run in the V8-gated lane. Required evidence
includes source-map-remapped exception spans, generated-location fallback when no map
applies, malformed-map fallback, and a clear statement that default non-V8 gates did not
execute V8 remapping.
ENGINE-15.CD diagnostic changes must keep default-lane renderer evidence deterministic:
stable code registry completeness, JSON source-frame snapshots, expanded redaction
coverage, and clear separation from V8-only async execution evidence. V8 async diagnostic
tests may prove JSON rendering for executed Promise paths only in the V8-enabled lane.
ENGINE-15.E diagnostic golden changes must update the diagnostics golden inventory, keep
text and JSON snapshots path-normalized and redacted, and explicitly report whether each
snapshot belongs to the default renderer lane or a V8-gated/process lane.
Plan-driven CLI consumer changes must keep process goldens deterministic for text and JSON
output, include audit nonzero coverage when ERROR findings are emitted, cover OpenAPI
partial metadata and Slop extensions, and keep runtime execution, V8, live-provider,
stress, package, and benchmark evidence separate unless those lanes are explicitly run.

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
outside the checkout, verifies required package files/stdlib assets, and builds a tiny
supported app with the packaged `sloppyc`. For non-V8 packages, `sloppy run --artifacts`
is expected to report V8 skipped/not configured rather than execute the handler. It does
not prove V8 runtime packaging, live providers, installers, signing/notarization,
package-manager distribution, or public alpha release readiness.

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
- Source-input run tests are split by gate: default non-V8 tests may compile source input
  and validate generated artifacts, but V8 execution remains a separate V8-enabled gate.
  Reports must distinguish source-input compiler handoff from V8 runtime success.
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

The scanner runs a self-test before the repository scan. The self-test creates temporary
fixtures that prove forbidden includes under `include/` and core `src/` fail with the
offending file/header, while matching includes under the allowed platform implementation
directories pass. The scanner remains a conservative lexical header-boundary check; it is
not a general C parser for every possible OS symbol reference.

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
`engine.v8.owner_thread`, `engine.v8.async_scheduler`, V8-gated bootstrap runtime tests,
and V8-gated `sloppy run`
process tests are compiled and linked only in that V8-enabled build. Passing default CTest
does not prove V8 smoke, owner-thread, lifecycle, Promise-policy, or bootstrap runtime
tests passed; the V8-enabled configure/build/test commands must be run and reported
separately.
The default non-V8 lane does run `core.execution_domain`, which proves the fixed ENGINE-26
domain policy table but does not prove V8 execution.
ENGINE-15.B adds V8 source-map remapping evidence to this lane. Passing it proves only the
bounded V8 exception primary-span remapping path for compiler-emitted maps; it does not
prove arbitrary bundler maps, async stack remapping, IDE source frames, package-manager
behavior, or public alpha readiness.
ENGINE-15.CD async diagnostic JSON evidence in this lane proves V8 Promise rejection
diagnostics can use the stable JSON renderer. It does not prove source-map-remapped async
stacks, arbitrary bundler source maps, Node/package-manager behavior, or public alpha
readiness.

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
- diagnostics snapshot tests and the diagnostics golden inventory;
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
## ENGINE-14 Gate Reporting

ENGINE-14 module-loading evidence follows the existing source-input split: compiler
module-graph tests can pass without V8, while source-input execution of generated module
artifacts is V8-gated. Default non-V8 success must not be reported as V8 runtime success.
