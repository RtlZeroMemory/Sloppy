# Roadmap

## Purpose

This roadmap is the source document for future GitHub EPICs, module-sized tasks,
implementation PR prompts, and reviewer prompts.

## Scope

It covers foundation through early performance validation:

- platform abstraction;
- agent harness and C standards;
- concurrency and async model;
- core C primitives;
- allocators;
- diagnostics;
- resource lifecycle;
- event loop;
- V8 smoke;
- plan loader;
- handwritten artifact execution;
- fake compiler emission;
- HTTP/router;
- public TypeScript API;
- ergonomics layer;
- modules;
- config/logging/services;
- capabilities;
- database providers;
- CLI introspection;
- benchmarks.

## Non-Goals

This roadmap is not a release promise. It does not authorize skipping tests, diagnostics,
ADRs, platform boundaries, or quality gates.

## GitHub Issue Source Files

GitHub-ready EPIC and task source files live under `docs/project/epics/` and `docs/project/tasks/`. The roadmap remains the phase narrative; project issues mirror these docs rather than replacing them.

## Roadmap Rules

- Each implementation PR should map to one epic and one bounded task.
- Architecture changes require docs and ADR updates.
- Feature code requires tests or an explicit test-plan exception.
- No runtime feature should land before the supporting foundation gates exist.

## EPIC: Documentation and Test Intent Governance

- Goal: lock documentation freshness and tests-as-intent before Phase 1 implementation.
- Non-goals: runtime features, V8, HTTP, routing, compiler extraction, providers.
- Prerequisites: foundation/spec/tooling docs.
- Tasks: documentation policy; user-facing docs skeleton; module docs skeleton; testing
  strategy; docs freshness checks; PR template updates; skill updates.
- Files likely touched: `docs/documentation-policy.md`, `docs/testing-strategy.md`,
  `docs/public/`, `docs/modules/`, `AGENTS.md`, `CONTRIBUTING.md`, `.github/`,
  `docs/skills/`, `tools/windows/`.
- Tests required: docs freshness checker if implemented; existing lint/test gates.
- Quality gates: docs freshness, lint, test, cargo gates.
- Acceptance criteria: docs policy exists; public docs skeleton exists; module docs
  skeleton exists; testing strategy exists; PR template enforces docs/test intent;
  `AGENTS.md` links to both policy docs.
- Reviewer checklist: no runtime feature code; examples marked planned when not
  implemented; tests-as-intent rule is visible in workflow docs and prompts.

## EPIC 00: Foundation/Spec/Tooling

- Goal: establish repository structure, documentation, ADRs, build skeleton, CI, formatting,
  linting, placeholder `sloppy`, and placeholder `sloppyc`.
- Non-goals: runtime features, V8, HTTP, routing, compiler extraction, providers.
- Prerequisites: empty Slop repository.
- Tasks: create specs, ADRs, CMake, presets, vcpkg manifest, scripts, CI, placeholders.
- Suggested issue split: repo skeleton; build skeleton; docs/ADRs; tooling/CI; placeholder
  CLIs.
- Files likely touched: root configs, `docs/`, `adr/`, `tools/`, `.github/`, `src/main.c`,
  `compiler/`.
- Tests required: CLI smoke, CTest, cargo tests, format/lint gates.
- Quality gates: bootstrap, configure, build, test, format-check, lint, artifact hygiene.
- Acceptance criteria: all placeholder gates pass; docs are story-ready; no generated
  artifacts tracked.
- Risks: specs remain too vague; tools pass locally but not in CI.
- Deferred decisions: license, release signing, final package layout.
- Reviewer checklist: no runtime feature code; docs match locked decisions; CI is realistic;
  ignored artifacts are not staged.

## EPIC 01: Platform Abstraction Skeleton

- Goal: enforce Windows-first, cross-platform-by-design platform boundaries.
- Non-goals: real OS abstraction functions unless needed for skeleton tests.
- Prerequisites: EPIC 00.
- Tasks: keep `src/platform/*`; define forbidden headers; run scanner; document future
  `sl_os_*` categories.
- Suggested issue split: scanner hardening; platform README review; future header design
  stub; CI lint integration.
- Files likely touched: `docs/platform-abstraction.md`, `src/platform/`,
  `include/sloppy/platform.h`, `tools/windows/check-platform-boundaries.ps1`.
- Tests required: scanner positive/negative fixture later; existing lint gate now.
- Quality gates: lint scanner passes; no OS headers outside platform dirs.
- Acceptance criteria: forbidden OS headers fail lint in core; platform dirs are the only
  allowed home for OS calls.
- Risks: `#ifdef _WIN32` spreads into core through convenience.
- Deferred decisions: exact `include/sloppy/os.h` public/private boundary.
- Reviewer checklist: no WinAPI/POSIX calls in core; docs updated for any new platform
  behavior; root tools remain forwarders.

## EPIC 01A: Agent Harness and C Standards

- Goal: make the repository legible, enforceable, and agent-ready before implementation.
- Non-goals: runtime features, V8, HTTP, routing, compiler extraction, providers.
- Prerequisites: EPIC 00.
- Tasks: `AGENTS.md`; agent-harness doc; C standards doc; review playbook; skills;
  execution-plan directories; standards scanner; CI integration.
- Suggested issue split: agent guide/docs; skills/playbooks; C standards scanner; quality
  score/debt tracker; CI/docs integration.
- Files likely touched: `AGENTS.md`, `docs/agent-harness.md`, `docs/c-standards.md`,
  `docs/review-playbook.md`, `docs/skills/`, `docs/exec-plans/`, `tools/windows/`,
  `.github/workflows/ci.yml`.
- Tests required: lint runs platform and C standards scanners; scanner failures are clear.
- Quality gates: bootstrap, configure, build, test, format-check, lint, cargo gates.
- Acceptance criteria: `AGENTS.md` is short and map-like; source docs are linked; standards
  scanner runs in lint; review playbook exists; skills exist; quality score and tech debt
  tracker exist.
- Risks: `AGENTS.md` grows into an encyclopedia; scanner false positives become noisy.
- Deferred decisions: strict allocator enforcement timing; scanner fixture strategy.
- Reviewer checklist: no runtime feature code; docs point to sources of truth; CI runs lint.

## EPIC 02: Core Basics: Status/Source Location/Strings/Bytes/Checked Math

- Goal: implement the first reusable C primitives safely.
- Non-goals: allocators/arenas beyond what primitives minimally need; diagnostics
  formatter; resource table.
- Prerequisites: EPIC 01.
- Tasks: implement `SlStatusCode`; implement `SlStatus`; add `SlSourceLoc`; add `SlStr`;
  add `SlBytes`; add `SlBuf` placeholder/design; add checked size arithmetic; add assert
  macros; wire C unit tests; update docs.
- Suggested issue split: status/source location; string/byte views; checked math; unit test
  harness; docs/headers ownership comments.
- Files likely touched: `include/sloppy/status.h`, `include/sloppy/source_loc.h`,
  `include/sloppy/string.h`, `include/sloppy/bytes.h`, `include/sloppy/checked_math.h`,
  `src/core/`, `tests/unit/core/`, `CMakeLists.txt`.
- Tests required: status success/failure; source loc defaults; empty/nonempty `SlStr`;
  embedded NUL string view; `SlBytes` zero length; checked add/mul overflow; assertion
  behavior where testable.
- Quality gates: CTest; clang-format; clang-tidy; warnings-as-errors in CI; platform
  scanner.
- Acceptance criteria: primitives compile cleanly as C17; headers document ownership and
  lifetimes; no raw `strlen` dependence except boundary tests; checked math detects
  overflow; tests are registered in CTest.
- Risks: string ownership semantics become ambiguous; tests overfit placeholder behavior.
- Deferred decisions: final UTF-8 policy; exact `SlBuf` ownership API.
- Reviewer checklist: public symbols use `sl_`; types use `SlTypeName`; no raw malloc/free
  outside future allocator; no VLA; no OS headers.

## EPIC 03: Allocators: SlArena

- Goal: add deliberate allocator and arena foundation.
- Non-goals: arena-everything architecture; async resource ownership; request lifecycle.
- Prerequisites: EPIC 02.
- Tasks: define allocator interface; implement simple heap-backed allocator wrapper; design
  `SlArena`; add permanent/startup/request/scratch arena docs; test alignment and reset.
- Suggested issue split: allocator interface; arena implementation; arena tests; memory docs
  update.
- Files likely touched: `include/sloppy/allocator.h`, `include/sloppy/arena.h`,
  `src/core/memory/`, `tests/unit/core/`.
- Tests required: allocation success/failure, alignment, reset, large allocation rejection,
  no raw malloc in callers.
- Quality gates: CTest, clang-tidy, sanitizer-ready behavior.
- Acceptance criteria: core code can allocate through Sloppy allocator APIs; arena ownership
  rules are documented and tested.
- Risks: allocator API locks too early; OOM diagnostics allocate recursively.
- Deferred decisions: debug allocation tracking detail.
- Reviewer checklist: no hidden global allocator mutation; cleanup paths are explicit.

## EPIC 04: Diagnostics Foundation

- Goal: implement structured diagnostics with stable codes and deterministic formatting.
- Non-goals: source map parser; full compiler diagnostics.
- Prerequisites: EPIC 02 and likely EPIC 03.
- Tasks: diagnostic structs; severity; source spans; related notes; formatter; snapshot
  harness; examples.
- Suggested issue split: data model; formatter; snapshot tests; first diagnostic examples.
- Files likely touched: `include/sloppy/diagnostics.h`, `src/core/diagnostics.c`,
  `tests/diagnostics/`.
- Tests required: missing service example, invalid plan version example, redaction fixture.
- Quality gates: snapshot tests; CTest; no machine-local path drift.
- Acceptance criteria: stable diagnostic code and text output can be tested.
- Risks: diagnostics allocate during OOM; messages become unstable.
- Deferred decisions: JSON diagnostics and localization.
- Reviewer checklist: diagnostic has code/severity/location; secrets redacted.

## EPIC 05: Resource Table/Lifecycle Kernel

- Goal: implement generation-counted resource IDs and lifecycle tracking.
- Non-goals: real file/db resources; JS bridge integration.
- Prerequisites: EPIC 03 and EPIC 04.
- Tasks: resource ID layout; table insert/get/close; generation increment; leak tracking;
  wrong-kind diagnostics.
- Suggested issue split: ID model; table operations; debug leak tracking; tests.
- Files likely touched: `include/sloppy/resource.h`, `src/core/resource.c`,
  `tests/unit/core/`.
- Tests required: stale ID, double close, wrong kind, generation reuse, leak report.
- Quality gates: CTest, sanitizer-ready, no raw pointer exposure.
- Acceptance criteria: JS-visible resources can be represented without raw pointers.
- Risks: ID layout ABI lock-in.
- Deferred decisions: 64-bit integer versus opaque JS object representation.
- Reviewer checklist: stale IDs fail; close is idempotent or diagnosed by policy.

## EPIC 06: Event Loop Abstraction

- Goal: define Sloppy-owned `SlLoop` boundary before libuv integration.
- Non-goals: full HTTP server; replacing libuv.
- Prerequisites: EPIC 05.
- Tasks: loop interface spec; lifecycle; timer/check smoke later; backend selection plan.
- Suggested issue split: header/API draft; null/test backend; docs/tests.
- Files likely touched: `include/sloppy/loop.h`, `src/core/loop.c`, `tests/unit/core/`.
- Tests required: create/destroy; run no-op; scheduled callback smoke if test backend exists.
- Quality gates: no libuv dependency before phase decision; CTest.
- Acceptance criteria: core depends on `SlLoop`, not libuv directly.
- Risks: overfitting to libuv or under-specifying async cleanup.
- Deferred decisions: scheduler and cancellation semantics.
- Reviewer checklist: backend boundary is explicit; no platform API leakage.

## EPIC 06A: Concurrency and Async Model

- Goal: define and later implement Sloppy's event-loop-per-JS-isolate async model.
- Non-goals: thread-per-request, libuv integration in the spec pass, worker implementation
  in v0.1, CPU-parallel JS inside one isolate.
- Prerequisites: EPIC 01A and EPIC 06 for implementation tasks.
- Source docs: `docs/concurrency.md`, ADR 0014, `docs/execution-model.md`,
  `docs/memory.md`.

Task: concurrency doc/ADR.

- Goal: make the async/threading model canonical.
- Non-goals: runtime code or dependencies.
- Files likely touched: `docs/concurrency.md`, `adr/0014-concurrency-and-async-model.md`,
  source-doc links.
- Tests required: manual doc acceptance until implementation exists.
- Acceptance criteria: owner-thread rule, request scope lifetime, provider async strategy,
  cancellation, backpressure, and worker scaling are specified.

Task: `SlLoop` abstraction.

- Goal: define a Sloppy-owned event loop boundary.
- Non-goals: real libuv/backend integration before its phase.
- Files likely touched: `include/sloppy/loop.h`, `src/core/loop*`, `src/platform/*`,
  `tests/unit/core/`.
- Tests required: create/destroy, no-op run, completion queue smoke with test backend.
- Acceptance criteria: core code depends on `SlLoop`, not a concrete OS or libuv API.

Task: JS owner-thread rule.

- Goal: encode one owner JS thread per V8 isolate.
- Non-goals: multiple workers from day one.
- Files likely touched: `src/engine/v8/`, future engine headers, diagnostics tests.
- Tests required: owner-thread assertions/checks and negative worker-pool entry tests.
- Acceptance criteria: no native worker thread can enter a shared isolate.

Task: V8 bridge owner-thread checks.

- Goal: make illegal cross-thread engine entry fail early.
- Non-goals: exposing V8 types to core.
- Files likely touched: `src/engine/v8/`, bridge diagnostics, CMake V8 test targets.
- Tests required: bridge smoke and wrong-thread diagnostic once V8 exists.
- Acceptance criteria: all engine entry points validate or inherit documented ownership.

Task: request scope async lifetime.

- Goal: keep request resources alive until promise settlement or cancellation cleanup.
- Non-goals: HTTP implementation before its phase.
- Files likely touched: request scope/resource modules, `docs/memory.md`, tests.
- Tests required: pending promise lifetime, cleanup-on-error, debug leak detection.
- Acceptance criteria: request arena and scoped services dispose exactly once.

Task: Promise settlement.

- Goal: convert returned values/promises into native response flow.
- Non-goals: full HTTP/routing.
- Files likely touched: `src/engine/v8/`, execution integration fixtures.
- Tests required: fulfilled promise, rejected promise, thrown exception, microtask behavior.
- Acceptance criteria: continuations run on JS thread and rejected promises produce
  route-aware diagnostics.

Task: native completion queue.

- Goal: post native async completions back to the JS owner.
- Non-goals: arbitrary thread-pool JS continuation.
- Files likely touched: `src/core/`, `src/platform/*`, test backend.
- Tests required: ordered completion dispatch, cancelled completion cleanup.
- Acceptance criteria: completion messages carry ownership/lifetime safely.

Task: worker pool abstraction.

- Goal: run blocking native work outside the JS thread.
- Non-goals: JS workers or CPU-heavy JS magic parallelization.
- Files likely touched: `include/sloppy/worker_pool.h`, `src/core/`, `src/platform/*`.
- Tests required: bounded queue, cancellation cleanup, no V8 entry contract.
- Acceptance criteria: worker tasks post completion and never call JS directly.

Task: DB provider async strategy.

- Goal: select per-provider async execution behind promise-friendly APIs.
- Non-goals: implementing SQLite/PostgreSQL/SQL Server in this epic.
- Files likely touched: `docs/data-providers.md`, provider modules later.
- Tests required: async transaction rollback and provider cancellation diagnostics later.
- Acceptance criteria: transactions pin resources until async callbacks settle.

Task: cancellation/deadlines.

- Goal: define request cancellation token and deadline propagation.
- Non-goals: provider-perfect cancellation on day one.
- Files likely touched: request scope, diagnostics, provider interfaces.
- Tests required: client disconnect, timeout, cleanup, unsupported provider cancellation.
- Acceptance criteria: cancelled requests do not resume the normal response path.

Task: backpressure.

- Goal: bound queues, bodies, DB checkout, and streaming response pressure.
- Non-goals: unbounded buffering for simplicity.
- Files likely touched: request limits, worker pool, DB pools, response writer.
- Tests required: queue limits, body limits, overload diagnostics.
- Acceptance criteria: overload returns controlled errors instead of unbounded memory growth.

Task: multi-worker design.

- Goal: scale JS execution across multiple workers/isolates later.
- Non-goals: shared mutable JS heap or multiple threads entering one isolate.
- Files likely touched: `docs/concurrency.md`, worker manager, app plan sharing code later.
- Tests required: worker distribution smoke, graceful shutdown, per-worker health later.
- Acceptance criteria: each worker has a separate isolate/event loop.

Quality gates: CTest, diagnostics snapshots, stress tests when implementation exists, no
V8 leakage, no platform leakage.

Risks: CPU-heavy JS blocks one worker; cancellation differs by provider; worker-pool queues
can become hidden memory growth if not bounded.

Reviewer checklist: no thread-per-request claims; no worker-pool V8 entry; request lifetime
matches pending promises; docs and ADR remain aligned.

## EPIC 07: V8 Bridge Smoke Test

- Goal: initialize V8 and call a trivial JS function through isolated C++ bridge.
- Non-goals: runtime app execution, HTTP, TypeScript compilation.
- Prerequisites: EPIC 04, EPIC 05, V8 SDK policy.
- Tasks: V8 SDK discovery; C bridge surface; isolate/context lifecycle; exception
  diagnostic; CMake CXX only under `src/engine/v8/`.
- Suggested issue split: SDK detection; bridge compile; call function smoke; exception
  smoke.
- Files likely touched: `src/engine/v8/`, `include/sloppy/engine.h`, `CMakeLists.txt`,
  `tools/windows/fetch-v8.ps1` later.
- Tests required: JS function returns value; thrown exception diagnostic; leak cleanup.
- Quality gates: no `v8::*` outside `src/engine/v8/`; build without V8 still works when
  engine disabled or phase not active.
- Acceptance criteria: C code calls bridge API without seeing V8 types.
- Risks: V8 SDK complexity and sanitizer mismatch.
- Deferred decisions: snapshots, module loading strategy.
- Reviewer checklist: bridge is isolated; no runtime feature sneaks in.

## EPIC 08: Plan Schema And Loader

- Goal: load and validate minimal `app.plan.json`.
- Non-goals: compiler extraction; route matching; DB providers.
- Prerequisites: EPIC 04 and JSON dependency decision.
- Tasks: choose/add JSON parser in phase; plan structs; validator; fixtures; diagnostics.
- Suggested issue split: schema fixture; parser integration; validator; handler table
  extraction.
- Files likely touched: `include/sloppy/plan.h`, `src/core/plan*.c`,
  `tests/golden/plan/`.
- Tests required: valid minimal plan, unsupported version, missing handler, duplicate ID,
  module cycle fixture.
- Quality gates: CTest; diagnostics snapshots; malformed input no crash.
- Acceptance criteria: handler table can be extracted from handwritten plan.
- Risks: schema churn; unknown field policy ambiguity.
- Deferred decisions: comments in dev JSON; schema file publication.
- Reviewer checklist: no secrets in fixtures; validation errors are diagnostic-rich.

## EPIC 09: Handwritten Artifact Execution Milestone

- Goal: runtime calls a handler by numeric ID from handwritten `app.js` and
  `app.plan.json`.
- Non-goals: HTTP, TS compiler, routing, modules.
- Prerequisites: EPIC 07 and EPIC 08.
- Tasks: handler registration intrinsic; plan/bundle consistency checks; synthetic context;
  result descriptor conversion; integration fixture.
- Suggested issue split: registration table; consistency validation; synthetic invocation;
  result conversion.
- Files likely touched: `src/engine/v8/`, `src/core/`, `tests/integration/execution/`.
- Tests required: success, missing handler, duplicate handler, thrown exception, rejected
  promise later.
- Quality gates: no V8 leakage; platform scanner; CTest integration.
- Acceptance criteria: CTest invokes handler ID `1` and receives expected text result.
- Risks: bridge coupling to plan internals.
- Deferred decisions: final JS module format and bootstrap ABI.
- Reviewer checklist: no HTTP/compiler logic; dispatch uses numeric ID.

## EPIC 10: sloppyc Fake Emitter

- Goal: emit deterministic fake artifacts that feed EPIC 09.
- Non-goals: Oxc, parsing, real extraction.
- Prerequisites: EPIC 08 plan schema.
- Tasks: CLI command; artifact writer; golden fixtures; cache metadata placeholder.
- Suggested issue split: CLI command; plan writer; JS writer; golden harness.
- Files likely touched: `compiler/src/`, `compiler/tests/`, `tests/golden/compiler/`.
- Tests required: cargo unit tests; golden output; invalid output path diagnostic.
- Quality gates: cargo fmt, clippy, cargo test.
- Acceptance criteria: fake artifacts run through handwritten execution harness.
- Risks: fake output mistaken for final schema behavior.
- Deferred decisions: project config filename.
- Reviewer checklist: no Oxc dependency; output deterministic.

## EPIC 11: HTTP/Router Foundation

- Goal: implement route pattern parsing and native route table basics.
- Non-goals: production HTTP server completeness; services/modules.
- Prerequisites: EPIC 08 and diagnostics foundation.
- Tasks: route pattern grammar; parser; matcher; ambiguity detection; handler ID lookup.
- Suggested issue split: pattern parser; route table; ambiguity diagnostics; fuzz target
  later.
- Files likely touched: `include/sloppy/router.h`, `src/core/router.c`,
  `tests/unit/router/`.
- Tests required: literals, params, typed params, ambiguous routes, no recursion blowups.
- Quality gates: CTest; fuzz plan; no unbounded parser recursion.
- Acceptance criteria: native route match returns handler ID for GET/POST fixtures.
- Risks: route syntax lock-in.
- Deferred decisions: llhttp integration timing.
- Reviewer checklist: parser is bounded; no HTTP feature creep beyond route foundation.

## EPIC 12: Public TypeScript API Bootstrap

- Goal: introduce initial JS/TS bootstrap API facade.
- Non-goals: full validation/modules/providers.
- Prerequisites: EPIC 09 and EPIC 10.
- Tasks: `Sloppy.createBuilder`; `Sloppy.create`; minimal `mapGet`; handler registration
  shape; `Results.text`.
- Suggested issue split: bootstrap package shape; tiny app fixture; Results.text; compiler
  fake integration.
- Files likely touched: future stdlib package, `compiler/src/emit/`, `src/engine/v8/`.
- Tests required: tiny app fixture emits/runs; missing handler diagnostic.
- Quality gates: golden artifacts; CTest integration.
- Acceptance criteria: tiny app shape is represented in artifacts and runs through milestone
  path.
- Risks: public API churn.
- Deferred decisions: package layout and module specifier resolution.
- Reviewer checklist: API matches ergonomics spec or spec is updated.

## EPIC 13: Developer Ergonomics Layer: Results/Routes/Groups/Validation Shape

- Goal: grow ergonomic app API deliberately.
- Non-goals: full OpenAPI generator; complex validators.
- Prerequisites: EPIC 12.
- Tasks: more `Results.*`; route groups; route names/tags; validation schema metadata;
  diagnostics for duplicates.
- Suggested issue split: Results helpers; route groups; validation metadata; diagnostics.
- Files likely touched: stdlib, compiler extraction/emit, plan schema, tests.
- Tests required: result descriptors; grouped route plans; validation plan entry.
- Quality gates: golden tests and diagnostics snapshots.
- Acceptance criteria: examples in `docs/developer-ergonomics.md` have implementation
  stories and tests.
- Risks: overbuilding schema DSL.
- Deferred decisions: final validation library ownership.
- Reviewer checklist: normal app path avoids raw request mutation.

## EPIC 14: Modularity/App Modules

- Goal: implement declarative phased app modules.
- Non-goals: native plugin ABI; compiler plugin API.
- Prerequisites: EPIC 13 and plan schema.
- Tasks: module metadata; phase execution/extraction; dependency sort; cycle diagnostics;
  plan contribution.
- Suggested issue split: module API shape; fake plan module output; topological sort; cycle
  diagnostics; graph freeze.
- Files likely touched: `include/sloppy/modules.h`, `src/modules/`, compiler extraction,
  `tests/golden/modules/`.
- Tests required: deterministic order; missing dependency; cycle; duplicate token.
- Quality gates: diagnostics snapshots; golden plan fixtures.
- Acceptance criteria: modules contribute routes/services/permissions deterministically.
- Risks: dynamic side effects undermine static extraction.
- Deferred decisions: typed service tokens.
- Reviewer checklist: import order is not behavior; graph freezes before run.

## EPIC 15: Config/Logging/Services

- Goal: implement core app-host services.
- Non-goals: enterprise DI container complexity.
- Prerequisites: EPIC 14.
- Tasks: config sources; logging sinks; service tokens; lifetimes; missing service
  diagnostics.
- Suggested issue split: service registry; config env/json; console logging; lifetime
  validation.
- Files likely touched: `src/core/services/`, `src/core/config/`, stdlib, plan schema.
- Tests required: missing service; duplicate service; scoped/singleton rules; redaction.
- Quality gates: CTest; diagnostics snapshots.
- Acceptance criteria: services validate before run and route-required services are known.
- Risks: overbuilding DI; secret leakage.
- Deferred decisions: structured logging API.
- Reviewer checklist: config secrets redacted; no hidden global service registry.

## EPIC 16: Filesystem/Capabilities

- Goal: implement capability-based filesystem foundation.
- Non-goals: OS sandboxing; broad fs API.
- Prerequisites: EPIC 01, EPIC 05, EPIC 15.
- Tasks: capability declarations; path normalization; resource IDs; read/write checks;
  audit metadata.
- Suggested issue split: capability plan schema; path API; fs resource model; denied
  diagnostics.
- Files likely touched: `src/platform/`, `src/core/permissions.c`, stdlib, tests.
- Tests required: allowed/denied path; path traversal; stale resource; redaction.
- Quality gates: platform scanner; no direct OS calls outside platform dirs.
- Acceptance criteria: no ambient filesystem power in normal JS API.
- Risks: cross-platform path semantics.
- Deferred decisions: symlink policy and OS sandboxing.
- Reviewer checklist: capabilities are explicit; diagnostics honest about non-sandboxing.

## EPIC 17: SQLite Provider

- Goal: implement first built-in/static database provider.
- Non-goals: dynamic provider ABI; multi-database abstraction completeness.
- Prerequisites: EPIC 05, EPIC 15, EPIC 16.
- Tasks: add sqlite dependency intentionally; provider module; query template lowering;
  transactions; cleanup diagnostics.
- Suggested issue split: dependency/build; common data API; SQLite open/query; transactions;
  plan integration.
- Files likely touched: `src/data/sqlite/`, `include/sloppy/data.h`, plan schema, stdlib.
- Tests required: query, queryOne, exec, transaction commit/rollback, statement cleanup.
- Quality gates: CTest/integration; diagnostics snapshots; no secret leakage.
- Acceptance criteria: SQLite-backed route fixture passes.
- Risks: bundling and file capability interaction.
- Deferred decisions: migrations.
- Reviewer checklist: raw SQL concat is not the blessed path; resources use IDs.

## EPIC 18: PostgreSQL Provider

- Goal: implement PostgreSQL provider via libpq.
- Non-goals: custom wire protocol; npm database package dependency.
- Prerequisites: EPIC 17.
- Tasks: add libpq dependency; driver/DLL strategy; pool; `$n` placeholder lowering;
  env-gated integration.
- Suggested issue split: libpq build/package; connection pool; query/exec; transactions;
  doctor diagnostics.
- Files likely touched: `src/data/postgres/`, build scripts, packaging docs.
- Tests required: missing config; missing libpq; env-gated query/transaction.
- Quality gates: integration skips clear; secrets redacted.
- Acceptance criteria: happy path query works when configured and unavailable diagnostic is
  actionable.
- Risks: DLL packaging and CI environment.
- Deferred decisions: test container strategy.
- Reviewer checklist: provider-specific APIs stay namespaced.

## EPIC 19: SQL Server Provider

- Goal: implement SQL Server provider via Microsoft ODBC Driver/ODBC API on Windows.
- Non-goals: ODBC for every database from the start.
- Prerequisites: EPIC 17 and platform diagnostics.
- Tasks: ODBC wrapper; driver detection; connection diagnostics; `?` placeholder lowering;
  env-gated integration.
- Suggested issue split: driver detection; ODBC connection; query/exec; transactions; doctor
  output.
- Files likely touched: `src/data/sqlserver/`, `src/platform/win32/`, build/package docs.
- Tests required: missing driver diagnostic; env-gated query; transaction rollback.
- Quality gates: platform boundary; secrets redacted.
- Acceptance criteria: missing Microsoft ODBC Driver diagnostic matches spec; configured
  query passes.
- Risks: machine-local driver variability.
- Deferred decisions: non-Windows SQL Server support.
- Reviewer checklist: ODBC calls stay in provider/platform boundary.

## EPIC 20: CLI Introspection: routes/doctor/audit/openapi

- Goal: add plan-powered developer tools.
- Non-goals: running user handlers for introspection.
- Prerequisites: EPIC 08, EPIC 14, EPIC 16, provider metadata as needed.
- Tasks: `sloppy routes`; `sloppy doctor`; `sloppy audit`; `sloppy openapi`; output golden
  tests.
- Suggested issue split: command framework; routes output; doctor checks; audit output;
  OpenAPI MVP.
- Files likely touched: CLI runtime, plan reader, docs, tests/golden/cli.
- Tests required: golden output; stale/missing plan diagnostics; redaction.
- Quality gates: CTest/golden tests; no handler execution.
- Acceptance criteria: commands explain app structure from plan artifacts.
- Risks: output format churn.
- Deferred decisions: JSON output flags.
- Reviewer checklist: tools use plan; diagnostics are actionable.

## EPIC 21: Benchmarks/Performance Validation

- Goal: measure performance claims with reproducible benchmarks.
- Non-goals: marketing numbers without data.
- Prerequisites: measurable paths from route/plan/handler/provider epics.
- Tasks: benchmark harness; metadata output; plan load benchmark; route match benchmark;
  handler dispatch benchmark; provider benchmark later.
- Suggested issue split: harness; first smoke benchmark; route benchmark; handler benchmark;
  report format.
- Files likely touched: `tests/benchmarks/`, docs, CI optional job.
- Tests required: benchmark smoke; metadata validation.
- Quality gates: correctness tests pass first; no benchmark-only code path changes runtime
  semantics.
- Acceptance criteria: every public performance claim links to benchmark data.
- Risks: noisy CI measurements.
- Deferred decisions: dashboard and comparison baselines.
- Reviewer checklist: benchmark states hardware/config; no unsupported claim is added.
