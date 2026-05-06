# Testing Strategy

## Purpose

Tests protect intended behavior, architecture boundaries, safety, diagnostics, and
developer ergonomics.

`docs/testing.md` remains the operational testing and layout guide. This document is the
canonical testing philosophy: what tests mean, how they relate to docs, and how reviewers
should judge test changes.

## Core Principle

Tests verify what the component/module/code is documented to do, not what the current
implementation accidentally does.

## Spec-Driven Testing

The workflow is:

1. docs/spec define intended behavior;
2. tests encode that behavior;
3. implementation changes until tests pass;
4. if intent changes, docs and tests change together.

Optional live provider tests are still tests, not silent best-effort probes. They must be
registered separately from default provider tests, report a CTest skip when required
environment variables or driver/service prerequisites are missing, and fail when a
configured live provider cannot satisfy the documented behavior. Skip output and failure
messages must identify missing prerequisites without printing secret values.

The MAIN1-13 conformance layer is the workflow-level guard for public alpha behavior. It
does not replace unit, golden, or provider tests; it ties together the real toolchain at
the boundaries users can rely on. Default conformance may compile sources, inspect emitted
artifacts, and assert clear unsupported diagnostics without requiring V8. V8-gated
conformance may execute `sloppy run --artifacts --once` and must be reported separately.
Unsupported or future behavior belongs in negative conformance or an explicit deferred
marker, not a fake passing fixture.

ENGINE-19.A defines the conformance evidence matrix in
`docs/project/engine-19-conformance-matrix.md`. New conformance work must keep the evidence
lanes separate: default non-V8, V8-gated, localhost transport, SQLite/capability, package
outside-checkout, live-provider optional, stress/smoke, and benchmark harness. Skipped
optional gates are not pass claims.

Post-ENGINE-16 Roadmap-2 testing must keep the same separation. ENGINE-26 concurrency
tests prove execution-domain and terminal-state invariants, ENGINE-27 tests prove feature
activation and missing-feature diagnostics, ENGINE-28 tests prove provider executor bridge
semantics, HTTP-26 tests prove route-level policy/observability, ENGINE-29 tests prove
event/counter reporting, and ENGINE-30 torture tests run only after those foundations are
mature. Torture or stress output is correctness evidence unless a later benchmark
methodology task explicitly scopes performance claims.

ENGINE-27.A/B adds `core.runtime_features` plus app-host and Plan parser coverage for the
registry foundation: deterministic feature ordering, route-derived HTTP/libuv/framework
activation, SQLite-provider activation only when Plan metadata requires it, unavailable
PostgreSQL/SQL Server feature failure, unknown `requiredFeatures[]`, missing
dependencies, and V8-disabled diagnostics. ENGINE-27.C/D extends the same default-lane
registry coverage with descriptor import/intrinsic metadata and adds V8-gated smoke
coverage proving SQLite intrinsics are registered for active `provider.sqlite` Plans and
omitted for Plans that do not activate that feature. ENGINE-27.E/F adds renderer-pinned
missing-feature goldens for unknown, unavailable, V8-disabled, provider, transport, and
dependency-missing runtime features plus a stdlib missing-intrinsic snapshot. These
goldens do not claim V8 execution, package trimming, or live-provider readiness.
CORE-FS-01.C/D/H extends this lane with default tests for the native filesystem resolver
and core operations, compiler-emitted `requiredFeatures[]` metadata from `sloppy/fs`
imports, bootstrap packaging of `stdlib/sloppy/fs.js`, and V8-gated smoke coverage for
the active `stdlib.fs` bridge. CORE-FS-01.E/F adds default native tests for directory,
temp, atomic, lock, binary FileHandle behavior, plus V8-gated smoke coverage for
directory and handle intrinsics. CORE-FS-01.G adds default native watch coverage for
create/modify/delete, stale close, no-event timeout, and unsupported recursive requests,
plus V8-gated smoke coverage for watch open/next/close. CORE-FS-02 adds default
runtime-artifact boundary coverage showing Plan/bundle/source-map loading reaches the V8
runtime boundary without an active app `stdlib.fs` feature. Later CORE-FS slices must keep
security golden and example evidence separate.

ENGINE-19.BC registers the current V8, HTTP, and async behavior under explicit conformance
names without adding runtime behavior. `conformance.http.default_dispatch` is default
non-V8 synthetic dispatch evidence, `conformance.transport.localhost_mvp` is localhost
transport MVP evidence, `conformance.async.*` is default native async/backend evidence,
and `conformance.v8.*` is V8-gated runtime evidence. These aliases make the evidence lane
visible while the existing unit/integration executables remain the detailed coverage.

ENGINE-17.E adds a V8-gated users API conformance proof that compiles a source SQLite app,
starts `sloppy run --artifacts` on localhost, sends raw TCP HTTP requests, and verifies
SQLite-backed JSON responses plus denied-capability and invalid-JSON failures. This is
workflow evidence, not benchmark, keep-alive, streaming, production-edge HTTP, public
alpha, PostgreSQL, or SQL Server evidence.

ENGINE-02.E adds source-input process coverage. Default non-V8 tests prove
`sloppy run <source>` and `sloppy run` with `sloppy.json` invoke `sloppyc`, emit
`app.plan.json`/`app.js`/`app.js.map`, validate artifacts, and then fail honestly at the
V8-required runtime boundary. V8-gated conformance adds executable source-input hello and
users-api-sqlite run-once cases. Source-input success in the default lane must not be
reported as V8 execution success.
CORE-FS-02 keeps fixture and trusted artifact reads separate from app filesystem policy:
tests that read checked-in fixtures or runtime artifacts are bootstrap/tooling evidence,
not proof that public `sloppy/fs` strict policy allowed or denied access.

CORE-CRYPTO-01 tests must keep evidence claims narrow. Contract tests prove
`sloppy/crypto` import recognition, `stdlib.crypto` Plan metadata, runtime-feature
diagnostics, stable crypto diagnostic names/goldens, and redaction helper coverage.
CORE-CRYPTO-01.E adds password hash/verify/needsRehash coverage for the selected
`libsodium` Argon2id PHC backend, unsupported-format checks, bootstrap stdlib smoke
coverage, and V8-gated owner-thread settlement evidence. CORE-CRYPTO-01.G adds
dependency-backed xxHash64 known-answer vectors, JS/V8 `NonCryptoHash` smoke coverage, and
doctor/golden coverage for security-looking static use. CORE-CRYPTO-01.I adds source
example API-shape checks and `tests/conformance/crypto/README.md` as the evidence index
across native vectors, diagnostic goldens, compiler/doctor checks, bootstrap stdlib tests,
and optional V8-gated bridge evidence. Deterministic tests must not claim randomness
quality, password cracking cost, timing resistance, benchmark performance, WebCrypto/Node/
Bun compatibility, or public alpha readiness.

CORE-NET-01 tests must keep evidence claims narrow. Contract tests prove `sloppy/net`
import recognition, `stdlib.net` Plan metadata, unavailable-feature diagnostics, stable
network diagnostic names/goldens, and network policy documentation. CORE-NET-01.C/D/H adds
deterministic loopback TCP client execution for connect/write/read/readLine/close and
embedded-NUL bytes. Default TCP execution tests must remain localhost/loopback only and
keep live-network, stress/torture, V8, package, and benchmark evidence separate.
CORE-NET-01.E/F adds deterministic loopback listener tests for ephemeral listen, accept,
accepted-connection ownership, listener close/stale behavior, and accept timeout. These
tests still do not claim external network, DNS, TLS, HTTP client, UDP, WebSocket, or
benchmark evidence.
CORE-CODEC-01 tests must keep evidence claims narrow. Contract tests prove `sloppy/codec`
import recognition, `stdlib.codec` Plan metadata, unavailable-feature diagnostics, stable
codec diagnostic names/goldens, backend policy, and checksum non-security wording.
CORE-CODEC-01.C/D/I adds default bootstrap vectors for Base64/Base64Url/Hex, UTF-8
fatal/replacement behavior, streaming partial sequences, BOM preservation, arbitrary-byte
roundtrips, deferred Compression/Checksum stubs, and V8-gated active/inactive
`__sloppy.codec` namespace smoke coverage. CORE-CODEC-01.E adds Binary reader/writer
endian, signed/unsigned width, 64-bit BigInt, bounds, seek, embedded-byte, and writer
capacity vectors. CORE-CODEC-01.F/G adds JS stdlib tests for compression option
validation, bounded async-iterable transform behavior, cancellation/deadline checks, and
V8-gated zlib gzip/gunzip roundtrip, corrupt stream, and decompression-limit evidence.
CORE-CODEC-01.H/J adds CRC32 known-answer vectors, source example shape checks,
checksum security-context compiler warnings, and `tests/conformance/codec/README.md` as
the evidence index. These tests still do not claim Node Buffer/Web Streams/Bun/Deno
compatibility, checksum security, benchmark performance, package readiness, or public
alpha readiness.

FRAMEWORK-01.B adds configuration coverage across Rust compiler tests, JS stdlib tests,
compiler golden artifacts, source-input process tests, and the SQLite users API fixture.
FRAMEWORK-01.F extends example coverage with compile-artifact tests for hello-minimal,
configured-api, modules-api, validation-errors, and users-api-sqlite, plus Plan-driven
routes/doctor/audit/capabilities/OpenAPI tooling checks for representative examples.
V8-gated users API transport covers happy path, missing user, create, malformed JSON, and
the current safe invalid-payload problem response.
Tests should keep source precedence, typed conversion, `bind`, provider config, Plan
metadata, redaction, and non-V8/V8 evidence boundaries separate.

## Test Categories

HTTP-25.A/B/C update: default localhost transport tests now include sequential HTTP/1.1
keep-alive reuse, `Connection: close`, keep-alive disabled config, HTTP/1.0 close policy,
idle timeout, max requests, request lifecycle reset, shutdown cleanup, and deterministic
pipelining rejection. These are correctness smoke/conformance checks, not benchmark,
production-edge, TLS, HTTP/2/3, SSE, WebSocket, file streaming, or public JS
streaming-helper evidence. HTTP-25.D/E adds default transport coverage for bounded chunked
request decoding, malformed chunk diagnostics, internal chunked streaming response
framing, final zero chunk emission, and pending-write backpressure rejection. HTTP-25.F
registers bounded keep-alive/chunked/streaming conformance aliases and a CI-friendly
stress smoke over the same localhost executable. That smoke repeats keep-alive reuse,
short-lived keep-alive connections, chunked requests, streaming responses, malformed
requests, and shutdown cleanup only; it is not throughput, latency, scalability, benchmark,
V8, public streaming API, or production-edge HTTP evidence.
ENGINE-16.A/B extends `core.app_host.hardening` with deterministic app lifecycle and scope
ownership cases: start success, double start, stop before start, startup failure cleanup,
graceful drain, forced shutdown, app/request identity propagation, request-scope rejection
after shutdown starts, request access after close, and app-scope resources surviving
request-scope close until app shutdown.
ENGINE-16.C extends the same target with terminal-outcome cleanup cases for success, sync
error, V8 exception, Promise rejection, validation/body parse failure, timeout, cancel,
client disconnect, response write failure, provider failure, provider pre-start cancel,
shutdown, and backpressure. It also covers stale late-completion rejection and typed
resource cleanup wrong-kind preservation through the typed cleanup opt-in. The execute
helper also has regressions for handlers that explicitly close or complete their own scope
before returning, for bare close rejection before terminal metadata is recorded, and for
late-completion rejection preserving the original terminal metadata. These are
deterministic native lifecycle tests, not runtime torture, benchmark, provider expansion,
or public API evidence.
ENGINE-16.D/E extends `core.app_host.hardening`, `core.resource.lifecycle`, and
`core.diagnostics.foundation` with leak-oriented evidence: app/request snapshots, resource
table snapshots, active resource counts by kind, late-completion counters, no-leak
assertions after request validation failure and app shutdown, synthetic leak diagnostics,
and stable lifecycle diagnostic code-name coverage. Timer, callback, and provider-operation
snapshot fields remain zero placeholders until those owners wire their runtime counters.

- C unit tests;
- Rust/`sloppyc` tests;
- compiler golden tests;
- plan fixture tests;
- diagnostics golden/snapshot tests;
- integration tests;
- structural/static scanner tests;
- fuzz tests;
- sanitizer tests;
- benchmarks;
- public API example tests later.

## Module Test Requirements

Core primitives:

- edge cases;
- invalid inputs;
- boundary conditions;
- overflow behavior;
- null/empty handling if allowed;
- ownership/lifetime behavior.

Memory/allocators:

- alignment;
- overflow rejection;
- mark/reset;
- out-of-memory behavior;
- debug poisoning if implemented;
- high-water stats if implemented.

Diagnostics:

- stable code;
- source span;
- related span;
- fix hint;
- golden text;
- deterministic JSON output;
- deterministic JSON source-frame output when matching source text is supplied.
- V8-gated source-map remapping tests must verify author-source primary spans, generated
  fallback when no map is attached, and malformed-map fallback without counting as default
  non-V8 evidence.

Resource table:

- stale ID;
- wrong kind;
- close/reuse;
- leak reporting;
- generation counter wrap strategy if relevant.

Time/deadline/cancellation:

- import-driven `stdlib.time` Plan activation and inactive-feature gating;
- native timer completion posts through `SlAsyncLoop` and settles only on the V8 owner
  thread;
- `Time.delay`, `Time.timeout`, `Deadline`, `CancellationController`, and `Time.yield`
  behavior in the V8-gated lane;
- deterministic JS stdlib validation for invalid delay and cancellation signal shape;
- timeout and cancellation errors remain distinguishable;
- interval async iteration, scheduled-job pause/resume/no-overlap skip behavior, and
  fake-clock deterministic delay/timeout/cleanup behavior in bootstrap stdlib tests;
- filesystem facade integration tests must cover pre-cancelled signals, expired deadlines,
  invalid `timeoutMs`, pass-through `Deadline.never`, and cancellation races without
  claiming native filesystem interruption;
- source example API-shape checks must cover delay, timeout, deadline, cancellation,
  interval, scheduled jobs, fake clocks, and filesystem Time options;
- diagnostic goldens cover all stable Time diagnostic codes.

Crypto:

- import-driven `stdlib.crypto` Plan activation and inactive-feature gating;
- OS-random shape tests must cover UUID v4 text shape, byte lengths, and token/hex/numeric
  alphabets without claiming randomness quality;
- SHA-256/SHA-384/SHA-512 and HMAC vectors must use vetted backends, not Sloppy custom
  algorithms;
- HMAC verification must use the constant-time comparison path;
- password hash/verify/needsRehash tests cover the selected Argon2id PHC backend and
  unsupported format diagnostics without logging passwords or encoded-hash internals;
- Secret disposal tests cover cleanup-once and stale-use behavior without printing secret
  bytes;
- `NonCryptoHash` vector and doctor-warning tests stay visibly separate from secure
  `Hash`/`Hmac` evidence;
- source example API-shape checks cover Random, Hash, Hmac, Password, ConstantTime, and
  Secret while rejecting weak-random fallbacks, compatibility claims, package-manager
  behavior, public alpha wording, benchmark claims, printed secrets, and non-security hash
  helpers in security examples.

Platform:

- OS-specific implementation tests under platform-specific test groups;
- core tests must not require OS APIs directly;
- platform boundary scanner.

Compiler:

- input fixture;
- expected `app.js`;
- expected `app.plan.json`;
- expected diagnostics;
- source map checks;
- deterministic, path-normalized golden outputs;
- no absolute local paths, timestamps, or random IDs in golden artifacts unless explicitly
  normalized and documented.
- library API fixture coverage for `compile_file(...)` and `compile_project(...)` as the
  compiler grows beyond a CLI-only surface.

COMPILER-30.A adds the first library/fixture harness layer around the existing compiler
goldens. It proves the CLI and library API build the current compiler hello artifact path,
invalid input returns structured diagnostics, source-map goldens remain stable, and staged
generated/cache artifacts are rejected by test coverage. This does not add inference
behavior.
COMPILER-30.H/I expands the compiler golden contract to strong Plan metadata: route and
plan completeness, source files, function modules, provider-kind-aware effects, and native
Plan compatibility fixtures for unknown optional fields versus unknown required features.
Tests should keep completeness cases explicit: complete route, partial response/body
metadata, invalid missing provider, and refreshed deterministic Plan goldens.
ENGINE-15.A expands source-map goldens to cover the `x_sloppy` metadata block, including
multi-file function modules, schema/provider/capability source locations, inferred effect
source context, source-file hashes, and source-input run map metadata. These tests prove
compiler artifact stability only; V8/runtime remapping remains a separate lane.
ENGINE-15.CD adds default-lane core diagnostic coverage for complete stable code registry
mapping, expanded shared redaction keys, and deterministic JSON source-frame snapshots.
V8 async rejection JSON remains V8-gated execution evidence and does not imply async stack
source-map remapping.
ENGINE-15.E expands deterministic diagnostic goldens under `tests/golden/diagnostics/` for
async JSON shape, capability denial source frames, malformed request-body JSON, and
redacted provider failure output. These goldens exercise the default renderer contract only;
V8 exception/remap, users API transport, live-provider, package, stress, and benchmark
lanes stay separate.
ENGINE-20.C/D add CLI golden coverage for consuming that metadata: route source/binding/
response/completeness output, generated capability output for SQLite effects, doctor
partial metadata checks, audit JSON warnings, audit nonzero behavior for ERROR findings,
OpenAPI supported-subset output, partial OpenAPI markers, and report-only optimization
candidates. These tests are static Plan/tooling coverage and do not prove handler
execution, V8, live providers, or runtime optimization.

Runtime execution:

- handwritten artifacts first;
- native completion queue skeleton before real async backends;
- async backend tests must separate deterministic test-backend coverage from libuv-backed
  coverage. Default native tests cover bounded capacity, overflow, cleanup-once behavior,
  scope retain/release, and libuv cross-thread post/owner-thread dispatch without
  requiring V8;
- provider/offload executor tests must run without V8 where possible and cover
  execution-mode validation, per-provider-instance bounded admission, serialized
  activation, copied input ownership, overflow/recovery, cancellation, timeout, shutdown,
  late completion, and cleanup exactly once. These tests prove the model shape, not live
  database throughput or SQLite async conversion;
- provider executor stress/smoke tests must be bounded and deterministic. They may prove
  many-operation admission, deterministic overflow, serialized one-active behavior,
  blocking-pool worker caps, cleanup-once behavior, shutdown safety, and redacted
  diagnostics/counters. They must not report throughput, latency, or public performance
  claims;
- HTTP backend stress/conformance smoke tests must follow the same evidence boundary. They
  may prove repeated valid parser/lifecycle paths, repeated malformed/parser-limit/body
  failures, unsupported media, overload rejection, shutdown/cancellation safety, cleanup
  once, and stable diagnostics. They must not use timing assertions, throughput/latency
  numbers, external-runtime comparisons, or production-edge HTTP claims;
- HTTP transport localhost smoke/conformance tests may use loopback TCP only. They should
  bind `127.0.0.1` with an ephemeral test port, send explicit HTTP/1.1 bytes through a raw
  client helper, assert complete response bytes, assert `Connection: keep-alive` or
  `Connection: close` policy plus `Content-Length`, and verify cleanup/counter coherence.
  HTTP-25.A/B/C coverage is sequential only: no second dispatch before the first response
  write completes, no concurrent requests on one connection, and deterministic close for
  pipelining attempts. They are correctness smoke, not benchmark, streaming, V8,
  live-provider, or production-edge evidence;
- V8-gated users API transport tests may combine compiler artifacts, `sloppy run`,
  localhost TCP, request body handling, V8 handler execution, and SQLite bridge calls.
  They must still report the V8 SDK prerequisite explicitly and must not describe the
  current synchronous SQLite bridge as async/offloaded provider execution;
- ENGINE-19.BC conformance registrations may reuse existing unit or integration
  executables when the executable already covers the documented behavior. The CTest name
  and labels must identify the evidence lane so reports do not blur default non-V8,
  V8-gated, localhost transport, and native async evidence;
- ENGINE-19.D conformance follows the same registration rule for SQLite and capability
  behavior. `conformance.sqlite.native_provider` is default native provider evidence;
  `conformance.capability.native_registry` and
  `conformance.capability.provider_executor` are default capability-policy/provider
  admission evidence; V8-gated `conformance.sqlite.bridge` and
  `conformance.sqlite.denied_capability` tests plus
  `conformance.users_api_sqlite.localhost_transport` are V8-gated and, for the users API,
  localhost transport evidence. Default SQLite/capability success must not be reported as
  V8 bridge, PostgreSQL/SQL Server bridge, live-provider, async SQLite offload, package,
  benchmark, public alpha, or production-edge HTTP evidence;
- HTTP-25.D/E streaming tests are transport correctness smoke for bounded chunked request
  decoding and internal/native chunked response writes. Future #446 keep-alive/streaming
  stress remains separate from this default smoke and should cover higher-volume reuse,
  richer socket backpressure timing, and shutdown/client-disconnect stress;
- native async settlement skeleton before V8 Promise integration;
- inline worker-pool completion skeleton before real worker threads;
- handler ID dispatch;
- V8 Promise settlement through the owner-thread microtask drain before async handler
  support is claimed;
- V8-gated native continuation tests must prove native completions settle or reject
  Promises only through the owner-thread scheduler and that wrong-thread dispatch fails
  before entering V8;
- `core.execution_domain` must prove the ENGINE-26 execution-domain policy in the default
  lane: only the V8 owner thread may enter V8, non-owner domains require copied/owned
  cross-thread data, and provider/offload domains must dispatch JavaScript continuation
  back to the owner thread;
- `core.async.backend` must prove the ENGINE-26.C/D generic terminal-completion guard:
  terminal completions skip dispatch, record late cleanup if configured, and still run
  cleanup plus scope release exactly once in the default lane;
- `core.cancellation.token` must prove that native cancellation reasons map to stable
  diagnostic categories through `sl_cancellation_diag_code`;
- `core.provider_executor` must prove the ENGINE-26.E blocking/offload policy helpers:
  only `INLINE_FAST` is owner-thread inline work, and serialized/pool-backed blocking
  modes require worker offload;
- bounded ENGINE-26.F race tests may cover terminal-after-enqueue, queued cancel,
  shutdown-while-queued/active, cleanup-once, and worker-claim edges. They must remain
  deterministic unit/conformance evidence and must not be reported as the ENGINE-30
  torture harness;
- route-aware diagnostics.

Plan schema fixtures:

- handwritten JSON fixtures under `tests/golden/plan/`;
- a fixture README or manifest that lists expected outcomes and diagnostic codes;
- fixture availability checks before production parsing exists;
- parser/validator tests verify documented valid and invalid fixture intent rather than
  current parser accident.

Public API:

- examples compile/run when feature exists;
- docs examples become tests where practical.
- CORE-FS-01.I/J adds deterministic CLI doctor/audit filesystem goldens plus source
  examples for `fs-basic`, roots/policy, streams, and watch; these examples remain source
  evidence until compiler extraction supports the full app-facing `sloppy/fs` surface.
- CORE-FS-02 adds runtime-artifact boundary coverage for artifact apps without
  `stdlib.fs`; app-facing strict-policy behavior remains covered by filesystem policy
  tests, not by trusted fixture reads.
- bootstrap module examples are statically checked until compiler extraction, real plan
  emission, and runtime module loading exist.
- bootstrap API-shape examples may be statically checked while compiler extraction, module
  loading, and HTTP serving remain future work, but the example docs must clearly say they
  are not runnable apps yet.
- JS/TS public API behavior must be tested through the V8 harness where possible.
- Static JS/TS fixture checks are acceptable only with a documented reason and are not a
  replacement for behavior tests.
- ENGINE-19.E package outside-checkout smoke must extract the archive outside the checkout,
  run packaged `sloppy`/`sloppyc` CLI startup checks, verify required package files and
  stdlib assets, build a tiny supported app with the packaged `sloppyc`, and report
  packaged `sloppy run --artifacts` separately. A default non-V8 package may prove compile
  and layout behavior while reporting artifact execution as skipped/not configured because
  V8 is unavailable; that is not V8 execution, release readiness, package-manager
  compatibility, live-provider evidence, or public alpha readiness;

## Test Naming / Layout

Target layout:

```text
tests/
  unit/
    core/
    memory/
    diagnostics/
    resource/
    platform/
  integration/
  golden/
    diagnostics/
    compiler/
    app-plan/
  fuzz/
  benchmarks/
```

ENGINE-19 conformance names should make the evidence lane visible. Prefer
`conformance.foundation.*`, `conformance.v8.*`, `conformance.http.*`,
`conformance.transport.*`, `conformance.sqlite.*`, `conformance.capability.*`,
`conformance.package.*`, `smoke.*`, and `benchmark.*` for new tests. Existing
`conformance.*` and `benchmarks.*` names may remain when their CTest labels and docs make
the lane clear.

## Red/Green Intent Discipline

If a test fails because implementation contradicts docs, fix implementation. If docs are
wrong, update docs and tests in the same PR.

Do not bless broken behavior by updating expected results without explaining the intent
change.

## Golden Tests

Golden tests are required for:

- diagnostics;
- compiler outputs;
- `app.plan` fixtures;
- public CLI output where stable.

Golden updates must explain the changed intent.

Diagnostic golden changes must also update `tests/golden/diagnostics/README.md` when they
add or remove a category, and must state the evidence lane they prove.

## Negative Tests

Every module should include negative tests:

- invalid input;
- overflow;
- stale resource;
- missing config;
- unsupported platform;
- permission denied;
- malformed plan;
- missing handler.

## Boundary Tests

Structural tests enforce:

- no OS headers outside platform directories;
- no V8 leakage outside `engine/v8`;
- no unsafe C functions;
- no generated artifacts;
- no Node/npm/package-manager assumptions in bootstrap JS/TS and examples;
- no unreviewed `unwrap`/`expect`/panic-style shortcuts in production Rust compiler code;
- future raw allocation restrictions.

Cross-platform CI is also a boundary test. Required Windows, Linux, and macOS jobs prove
the default non-V8 runtime/compiler/test surface on each runner. Optional V8 and live
provider jobs must be reported separately from required default CI, because skipped V8 SDKs
or missing database connection environment variables do not prove those paths.

## Benchmarks

Benchmarks validate performance claims. They do not replace correctness tests.

Benchmark smoke runs only prove that the harness starts and the selected benchmark paths
execute with tiny iteration counts. A benchmark must not become a public performance claim
unless the exact command, build configuration, hardware/context, and output are reported.
Release builds are required for meaningful local numbers.

Allocation-aware smoke tests should use deterministic resource limits rather than timing or
platform allocator behavior. For memory/string adoption work, prefer small caller-owned
arenas, fixed builder capacities, and expected `SL_STATUS_CAPACITY_EXCEEDED` or successful
low-capacity operation over heap-allocation counters.

## Acceptance Criteria for Phase 1 Testing

For Phase 1 core primitives:

- unit test harness works;
- tests are tied to `docs/c-standards.md`, `docs/memory.md`, and `docs/modules/core/`;
- edge cases are covered;
- invalid input behavior is covered;
- CI/CTest runs tests.

## Reviewer Checklist

Reviewers must ask:

- What intended behavior does this test protect?
- Which doc/spec defines that behavior?
- Is this testing implementation details unnecessarily?
- Are negative cases covered?
- Would this test catch a real regression?
- Did docs change if expected behavior changed?
## ENGINE-14 Test Split

ENGINE-14 module tests are split by evidence lane. Default compiler/golden tests cover
relative import success and failure, provider import rewriting, function-module Plan
contribution, dynamic import rejection, and multi-source source maps. V8/source-input
execution remains V8-gated; passing default non-V8 tests must not be reported as V8 module
runtime success.
