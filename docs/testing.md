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

- CTest unit tests for core status, source location, string view, byte view, checked math,
  arena behavior, native cleanup scope behavior, native completion queue behavior, native
  async settlement behavior, inline worker-pool completion contract behavior, route pattern
  parser/matcher behavior, HTTP request-head parser behavior, libuv dependency smoke,
  minimal plan contract helper behavior, minimal plan JSON parser/validator behavior,
  diagnostics foundation behavior, and assertion macro compilation;
- CTest smoke for `sloppy --version`;
- CTest smoke for `sloppy --help`;
- CTest smoke for `sloppyc --version`;
- CTest structural check `bootstrap.stdlib.assets`, which verifies the bootstrap stdlib
  source files exist and were copied to the build output support-data layout;
- CTest structural check `bootstrap.stdlib.api_shape`, which statically verifies the tiny
  bootstrap API source shape for `Results.text/json`, `Sloppy.create`,
  `Sloppy.createBuilder`, builder config/logging/services, `app.mapGet`, `app.mapGroup`,
  route group metadata, `.withName`, `app.freeze`, route snapshots, `schema`, index
  exports, and absence of deferred app-host APIs;
- optional CTest executable check `bootstrap.stdlib.app_host_foundation` when `node` is
  available, which imports the ESM stdlib and verifies builder freeze, config behavior,
  logging memory sinks, services singleton/transient behavior, route groups, result
  helpers, schema validation, route handler context, and app freeze behavior. This is test
  infrastructure only and is not a Node compatibility claim;
- optional CTest executable check `bootstrap.stdlib.modules` when `node` is available,
  which imports the ESM stdlib and verifies `Sloppy.module`, `builder.addModule`, module
  dependency order, missing dependency/cycle/duplicate diagnostics, phase failure context,
  route/service attribution, and module debug metadata. This is test infrastructure only
  and is not a Node compatibility claim;
- optional CTest executable check `bootstrap.stdlib.data_foundation` when `node` is
  available, which imports the ESM stdlib and verifies database capability metadata, query
  template lowering, fake data provider query/queryOne/exec behavior, transaction
  commit/rollback behavior, rejected async callback rollback, nested transaction rejection,
  use after closed transaction scope, and module/service integration. This is test
  infrastructure only and is not a Node compatibility claim;
- CTest structural check `examples.hello.api_shape`, which statically verifies the first
  hello example files exist, use the current relative stdlib import, use
  `Sloppy.createBuilder`, `builder.build`, `app.mapGet`, and `Results.text`, and do not
  introduce package-manager scope;
- CTest structural check `examples.ergonomics.api_shape`, which statically verifies the
  EPIC-13 example files exist, use the current relative stdlib import, demonstrate route
  groups, result helpers, schema metadata, config/logging/services, and honestly document
  that the example is not runnable through `sloppy run` yet;
- CTest structural check `examples.modules_basic.api_shape`, which statically verifies the
  EPIC-14 module example files exist, use the current relative stdlib import, demonstrate
  module dependencies, services, routes, metadata, and builder registration, and honestly
  document that the example is not runnable through `sloppy run` yet;
- CTest structural check `examples.data_foundation.api_shape`, which statically verifies
  the EPIC-15 data foundation example files exist, use the current relative stdlib import,
  demonstrate database capability metadata, fake data provider service registration, query
  template lowering, transaction skeleton usage, and honestly document that the example is
  not runnable and has no real database provider yet;
- Rust unit tests for placeholder CLI argument behavior;
- platform-boundary scanner;
- C standards scanner.

When V8 is explicitly enabled and a valid SDK is configured, CTest also registers
`engine.v8.smoke`. That test evaluates classic JavaScript source, calls a named global
function returning `sloppy-ok`, and checks syntax errors, missing/non-callable globals,
throwing functions, and unsupported result types fail with diagnostics instead of crashing.
TASK 08.A also registers `execution.handwritten_artifact`, which parses the handwritten
plan fixture, evaluates handwritten `app.js`, invokes handler ID `1`, and covers missing
handler ID, missing JS function, and thrown handler diagnostics. These are not part of the
default non-V8 test set.

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
  test_string.c
  test_bytes.c
  test_checked_math.c
  test_arena.c
  test_scope.c
  test_loop.c
  test_async.c
  test_worker_pool.c
  test_http.c
  test_route.c
  test_plan.c
  test_plan_parse.c
  test_diagnostics.c
  test_assert.c
  test_source_loc_cpp.cpp
tests/unit/platform/
  test_platform_boundary_docs.c   # only if useful; scanner remains script-based
tests/golden/diagnostics/
  missing_service.snap
  invalid_plan_version.snap
tests/golden/plan/
  README.md
  valid-minimal.plan.json
  valid-multiple-handlers.plan.json
  unknown-future-field.plan.json
  malformed-json.plan.json
  invalid-version.plan.json
  missing-runtime-minimum-version.plan.json
  duplicate-handler-id.plan.json
  missing-bundle.plan.json
  missing-bundle-path.plan.json
  missing-source-map.plan.json
  missing-handlers.plan.json
  empty-handlers.plan.json
  invalid-handler-id.plan.json
  missing-handler-export.plan.json
  empty-handler-export.plan.json
  wrong-field-type.plan.json
tests/golden/compiler/
  README.md
```

CTest naming should expose subsystem and behavior:

```text
core.status.success
core.str.slice
core.checked_math.overflow
core.plan.contract
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

munit is still a possible future C framework, but TASK 02.A uses a tiny dependency-free C
test style: each test source is a small executable that returns nonzero on failure and is
registered by CTest.

possible munit integration plan:

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
tests/golden/diagnostics/
  invalid_plan_version.snap
  missing_service.snap
```

TASK 04.A wires `core.diagnostics.foundation` to read and compare those fixture files.
Snapshot drift fails CTest unless the expected fixture change is intentional.

## Integration Tests

Integration tests start with the handwritten app execution milestone.

First target:

```text
handwritten app.js + handwritten app.plan.json -> runtime calls handler by ID
```

The first target is implemented as a V8-gated CTest integration executable:

```text
tests/integration/execution/test_handwritten_artifact_execution.c
tests/integration/execution/handwritten_smoke/app.plan.json
tests/integration/execution/handwritten_smoke/app.js
```

It uses the runtime contract helper directly and does not start HTTP, route matching,
compiler output loading, public TypeScript APIs, modules, services, data providers, or an
async event loop.

TASK 10.C adds a second V8-gated integration fixture for synthetic HTTP dispatch:

```text
tests/integration/http_dispatch/test_http_dispatch_execution.c
tests/integration/http_dispatch/fixtures/app.plan.json
tests/integration/http_dispatch/fixtures/app.js
```

It parses an in-memory HTTP GET request head, matches a manual route binding, resolves the
numeric handler ID through the parsed plan, and invokes the existing runtime-contract
helper. It still does not start sockets, write responses, parse bodies, build request
contexts, run middleware, or exercise public TypeScript APIs.

TASK 11.B/11.C adds the first non-executing bootstrap stdlib API-shape check, and
TASK 12.A/12.B/12.C/12.D expands it for the app-host foundation skeleton:

```text
tests/cmake/check_bootstrap_api.cmake
```

It is intentionally static because the current V8 bridge smoke path evaluates classic
scripts only and does not load ESM modules from `stdlib/sloppy/`. A future module-loading
task should replace or supplement this with executable bootstrap stdlib tests.

TASK 12 also adds an optional executable ESM smoke test:

```text
tests/bootstrap/test_app_host_foundation.mjs
```

CMake registers it as `bootstrap.stdlib.app_host_foundation` only when `node` is available.
It verifies documented bootstrap behavior for builder/app freeze, config, logging,
services, route groups, result descriptors, schema validation, route context, and
`Sloppy.create()` consistency. It does not add package-manager behavior, npm dependencies,
or a Node compatibility promise. V8-backed ESM bootstrap tests remain future work.

TASK 14 adds a second optional executable ESM smoke test:

```text
tests/bootstrap/test_modules.mjs
```

CMake registers it as `bootstrap.stdlib.modules` only when `node` is available. It verifies
documented bootstrap behavior for module API shape, builder integration, dependency graph
ordering, module diagnostics, phase error context, route/service attribution, and module
debug metadata. It does not add package-manager behavior, npm dependencies, runtime module
loading, or a Node compatibility promise.

EPIC-15 adds a third optional executable ESM smoke test:

```text
tests/bootstrap/test_data_foundation.mjs
```

CMake registers it as `bootstrap.stdlib.data_foundation` only when `node` is available. It
verifies documented bootstrap behavior for capability metadata, query template lowering,
fake data providers, transaction callback behavior, and module/service integration. It does
not add package-manager behavior, npm dependencies, database connections, SQL execution, or
a Node compatibility promise.

TASK 11.D adds the first public example structural check:

```text
examples/hello/app.js
examples/hello/README.md
tests/cmake/check_hello_example.cmake
```

It is intentionally static for the same ESM/module-loading reason. It verifies documented
example intent and current API usage without requiring Node, npm, a bundler, `sloppy run`,
compiler extraction, `app.plan.json` emission, or HTTP server behavior.

EPIC-13 adds a second static example check:

```text
examples/ergonomics/app.js
examples/ergonomics/README.md
tests/cmake/check_ergonomics_example.cmake
```

It verifies the current route group, result helper, schema skeleton, and app-host foundation
API shape without claiming runtime execution.

TASK 14 adds the first module example structural check:

```text
examples/modules-basic/app.js
examples/modules-basic/README.md
tests/cmake/check_modules_basic_example.cmake
```

It verifies the current module API shape without claiming compiler extraction, real data
providers, HTTP serving, package loading, or runtime execution.

EPIC-15 adds the first data foundation example structural check:

```text
examples/data-foundation/app.js
examples/data-foundation/README.md
tests/cmake/check_data_foundation_example.cmake
```

It verifies the current data/capability API shape without claiming compiler extraction,
real database providers, database connections, SQL execution, HTTP serving, package
loading, or runtime execution.

Later integration tests cover HTTP, routing, modules, providers, and packaging.

## Async and Concurrency Tests

Future concurrency tests should cover:

- native async settlement over `SlLoop`;
- V8 Promise settlement and rejected promise diagnostics;
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
- CTest includes status, source location, string, bytes, checked-math, arena, and scope
  tests;
- no primitive API lands without ownership/lifetime tests.
- native completion queue tests cover FIFO dispatch, capacity exhaustion, callback failure,
  stop/reset, and single-threaded drain behavior.
- native async settlement tests cover pending/fulfilled/rejected/cancelled states, loop-post
  dispatch, double settlement failure, failed-post atomicity, borrowed diagnostics,
  reinit-before-drain rejection, and continuation failure propagation.
- inline worker-pool tests cover submit validation, posted-not-inline completion dispatch,
  success/failure status forwarding, FIFO completion order, queue-full result destruction,
  loop-reset cleanup, reinit-before-drain rejection, and completion failure propagation
  without real threads.

Plan loader phase:

- valid/invalid plan fixtures;
- fixture README or manifest documenting expected outcomes;
- fixture existence checks before the parser exists;
- parser tests once the parser exists;
- diagnostics checks;
- malformed JSON tests.

V8 smoke phase:

- engine initialization smoke;
- classic script/global function smoke;
- thrown function failure smoke;
- syntax error diagnostic smoke;
- missing/non-callable function diagnostic smoke;
- handwritten plan handler ID to JS global smoke;
- handler registration smoke later.

HTTP/router phase:

- route parser/matcher unit tests; started in TASK 10.A with `core.route.pattern`;
- HTTP request-head parser tests; started in TASK 10.B with `core.http.parser`;
- synthetic GET route dispatch tests; started in TASK 10.C with `core.http.dispatch` and
  V8-gated `http.dispatch.execution`;
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
