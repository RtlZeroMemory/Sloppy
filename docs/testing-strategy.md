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
- source map checks.

Runtime execution:

- handwritten artifacts first;
- handler ID dispatch;
- promise settlement later;
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
- future raw allocation restrictions.

## Benchmarks

Benchmarks validate performance claims. They do not replace correctness tests.

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
