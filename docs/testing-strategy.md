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

## Test Categories

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
- JSON output later.

Resource table:

- stale ID;
- wrong kind;
- close/reuse;
- leak reporting;
- generation counter wrap strategy if relevant.

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
  client helper, assert complete response bytes, assert `Connection: close`/`Content-Length`
  policy for the implemented one-request-per-connection MVP, and verify cleanup/counter
  coherence. They are correctness smoke, not benchmark, keep-alive, pipelining, streaming,
  V8, live-provider, or production-edge evidence;
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
- future keep-alive and streaming tests must be reported as a separate HTTP/1.1 upgrade
  evidence set. They must cover sequential requests per connection, read-loop resume only
  after response write completion, idle timeout, max requests per connection, shutdown
  drain/force-close behavior, request-arena and parser/body lifecycle reset between
  requests, client close while idle or between requests, chunked request decoding,
  chunked/streaming response writing, socket backpressure, and cancellation of partially
  consumed body streams;
- native async settlement skeleton before V8 Promise integration;
- inline worker-pool completion skeleton before real worker threads;
- handler ID dispatch;
- V8 Promise settlement through the owner-thread microtask drain before async handler
  support is claimed;
- V8-gated native continuation tests must prove native completions settle or reject
  Promises only through the owner-thread scheduler and that wrong-thread dispatch fails
  before entering V8;
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
- bootstrap module examples are statically checked until compiler extraction, real plan
  emission, and runtime module loading exist.
- bootstrap API-shape examples may be statically checked while compiler extraction, module
  loading, and HTTP serving remain future work, but the example docs must clearly say they
  are not runnable apps yet.
- JS/TS public API behavior must be tested through the V8 harness where possible.
- Static JS/TS fixture checks are acceptable only with a documented reason and are not a
  replacement for behavior tests.

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
