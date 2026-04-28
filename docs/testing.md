# Testing

## Purpose

Testing is part of Sloppy's architecture. This document defines what tests exist now, when
future tests start, how they are named and laid out, and what acceptance criteria apply per
phase.

For the canonical testing philosophy and docs-as-intent model, see
`docs/testing-strategy.md`. This file is the operational testing category and gate guide;
`docs/testing-strategy.md` defines what tests mean.

## Scope

This document covers:

- C unit tests;
- Rust tests;
- compiler golden tests;
- diagnostics snapshot tests;
- integration tests;
- fuzz tests;
- sanitizer tests;
- platform-boundary tests;
- static structural checks;
- async/concurrency tests;
- benchmarks.

## Non-Goals

The foundation phase does not vendor munit, add fuzz targets, or implement runtime feature
tests.

## Current Phase

Current tests:

- CTest smoke for `sloppy --version`;
- CTest smoke for `sloppy --help`;
- CTest smoke for `sloppyc --version`;
- Rust unit tests for placeholder CLI argument behavior;
- platform-boundary scanner;
- C standards scanner.

## Future Phase

Testing expands with each implementation epic. No feature story should land without either
tests or a documented reason why the story is spec-only.

Tests should be written from documented intended behavior, not from accidental current
implementation behavior. If intended behavior changes, update the relevant docs and tests
in the same PR.

## Public API Shape

Tests are invoked through:

```powershell
.\tools\windows\dev.ps1 test
.\tools\windows\dev.ps1 format-check
.\tools\windows\dev.ps1 lint
cargo test --manifest-path compiler/Cargo.toml
```

## Test Layout

Target layout:

```text
tests/
  unit/
  integration/
  golden/
  fuzz/
  diagnostics/
  benchmarks/
```

Rust compiler tests may live in `compiler/src/` for unit tests and `compiler/tests/` for
integration/golden harnesses.

First Phase 1 layout:

```text
tests/unit/core/
  test_status.c
  test_source_loc.c
  test_str.c
  test_bytes.c
  test_checked_math.c
tests/unit/platform/
  test_platform_boundary_docs.c   # only if useful; scanner remains script-based
tests/diagnostics/
  README.md
tests/golden/compiler/
  README.md
```

CTest naming should expose subsystem and behavior:

```text
core.status.success
core.str.slice
core.checked_math.overflow
compiler.cli.version
```

## Naming Rules

- C unit tests: `test_<module>_<behavior>`;
- integration tests: `<feature>.integration`;
- golden fixtures: `<feature>/<case>.expected`;
- diagnostics snapshots: `<diagnostic-code>.snap`;
- fuzz targets: `fuzz_<parser_or_boundary>`;
- benchmarks: `bench_<claim_or_path>`.

## C Unit Tests

C unit tests start in Phase 1 with core primitives.

Required for:

- status;
- strings/bytes/buffers;
- checked math;
- allocators;
- arenas;
- diagnostics;
- resource table;
- platform abstraction.

munit is the planned C framework, but it is not vendored yet.

munit integration plan:

1. add munit only when first real C unit test lands;
2. keep it in an explicit third-party/vendor location or documented dependency path;
3. wire one test executable per bounded subsystem where practical;
4. register each executable with CTest;
5. keep test names stable for CI triage.

## Rust Tests

Rust tests apply to `sloppyc`.

Required gates:

- `cargo fmt --check`;
- `cargo clippy -- -D warnings`;
- `cargo test`.

Unit tests should cover CLI parsing and pure compiler helpers. Golden tests should cover
emitted artifacts once emission begins.

## Compiler Golden Tests

Compiler golden tests start with the fake plan emitter. They should cover:

- `app.plan.json`;
- generated handler IDs;
- source map fragments;
- diagnostics;
- module ordering;
- data provider extraction.

Golden updates require review because artifacts are public contracts.

Expected layout:

```text
compiler/tests/golden/
  fake-plan/basic/
    input/
    expected/app.plan.json
    expected/app.js
    expected/app.js.map
```

## Diagnostics Snapshot Tests

Diagnostics snapshots start with diagnostics foundation and compiler extraction.

They should verify:

- machine-readable code;
- severity;
- message;
- source span;
- code frame;
- suggested fix;
- related locations.

Expected layout:

```text
tests/diagnostics/
  snapshots/
    SLP_PLAN_UNSUPPORTED_VERSION.snap
    SLP_SERVICE_MISSING.snap
```

## Integration Tests

Integration tests start with the handwritten app execution milestone.

First target:

```text
handwritten app.js + handwritten app.plan.json -> runtime calls handler by ID
```

Later integration tests cover HTTP, routing, modules, providers, and packaging.

## Async and Concurrency Tests

Future concurrency tests should cover:

- promise settlement and rejected promise diagnostics;
- request scope lifetime across pending promises;
- cancellation cleanup;
- worker-pool no-V8-entry contract;
- async resource leak detection;
- async DB transaction rollback;
- stress tests for many in-flight requests without thread-per-request behavior.

## Fuzz Tests

Fuzz tests start when untrusted parsers exist.

Targets:

- plan JSON parser;
- route pattern parser;
- HTTP boundaries where Sloppy parses input;
- config parser;
- diagnostics/source map parser;
- compiler extraction boundaries where applicable.

Fuzz targets should use `fuzz_<boundary>` naming, for example:

- `fuzz_plan_json`;
- `fuzz_route_pattern`;
- `fuzz_source_map`;
- `fuzz_config_json`.

## Sanitizer Tests

ASan/UBSan should run where the toolchain supports them. Windows support may be partial,
especially once V8 is introduced. Core-only sanitizer configurations remain valuable.

## Platform-Boundary Tests

The platform-boundary scanner runs now. It fails if forbidden OS headers appear outside
platform implementation directories.

Future platform tests should live in platform-specific suites and CI jobs.

Scanner test expectations:

- one fixture with forbidden include in core fails;
- one fixture under platform directory passes;
- CI lint runs scanner with repository paths.

## Static Structural Checks

Static structural checks are tests for repository boundaries. They catch violations before
runtime tests exist and should be treated as part of the test strategy.

Current checks:

- platform boundary violations;
- V8 leakage outside the bridge;
- unsafe C functions;
- generated artifact hygiene.

Future checks:

- allocator misuse;
- resource ID/lifetime misuse;
- docs drift where source-of-truth links can be checked mechanically.

## Benchmark Tests

Benchmarks start only when there is behavior worth measuring. No performance claim without
a benchmark.

Benchmark reports must include:

- commit;
- OS/hardware;
- build type;
- workload;
- compared baseline;
- repetition/statistics.

## Acceptance Criteria Per Phase

Phase 1:

- C primitive unit tests exist;
- platform-boundary scanner passes;
- sanitizer-ready code.
- CTest includes status, source location, string, bytes, and checked-math tests;
- no primitive API lands without ownership/lifetime tests.

Plan loader phase:

- valid/invalid plan fixtures;
- diagnostics snapshots;
- malformed JSON tests.

V8 smoke phase:

- engine initialization smoke;
- handler registration smoke;
- exception diagnostic smoke.

HTTP/router phase:

- route parser unit tests;
- route match integration tests;
- fuzz target for route patterns.

Provider phases:

- parameter binding;
- transaction commit/rollback;
- cleanup/leak behavior;
- driver-unavailable diagnostics.

Benchmark phase:

- benchmark smoke runs;
- metadata is recorded;
- no product claim is made without stored results.

## Quality Gates

- CMake build;
- CTest;
- cargo tests;
- clang-format;
- clang-tidy;
- rustfmt;
- clippy;
- platform scanner;
- C standards scanner;
- artifact hygiene.

## Development Tasks

- Add C unit framework before core primitive implementation grows.
- Add diagnostics snapshot harness before diagnostics become complex.
- Add golden fixture convention before fake compiler emission.
- Add integration fixture harness before handwritten app execution milestone.
- Add fuzz harness once first untrusted parser lands.

## Open Questions

- Exact C test framework vendoring approach.
- Whether golden tests use insta-like snapshots in Rust.
- How sanitizer CI is split between core-only and V8-enabled builds.
