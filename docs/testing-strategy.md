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
- native async settlement skeleton before V8 Promise integration;
- inline worker-pool completion skeleton before real worker threads;
- handler ID dispatch;
- V8 Promise settlement later, required before async handler support is claimed;
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
