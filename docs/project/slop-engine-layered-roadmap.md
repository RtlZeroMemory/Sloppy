# Slop Engine Layered Roadmap

Status: proposed strategic roadmap.

This roadmap is layered on purpose. It finishes core engine/framework building blocks
before userland/framework perks, benchmark claims, or public alpha docs.

## Bounded-Context PR Chunking

Issue tasks are planning atoms, not a mandate for one tiny PR per task. Prefer mid-large
bounded-context PRs that group several related tasks when they share ownership, tests,
docs, and risk. Split only when a change crosses a hard boundary, becomes hard to review,
or would mix unrelated implementation surfaces.

Recommended implementation chunks:

| Chunk | Likely issues | PR shape |
| --- | --- | --- |
| Contract lock | ENGINE-01.A through ENGINE-01.D | One contract PR for JS API, HTTP, async/cancellation, SQLite, and resource-limit decisions. |
| Compiler/Plan pipeline | ENGINE-02.A through ENGINE-02.D | One large-coherent compiler+Plan PR, or split source maps only if diagnostics risk dominates. |
| V8 async runtime | ENGINE-03.A through ENGINE-03.D plus ENGINE-08.B | One large-coherent V8 async PR covering Promise settlement, microtasks, cancellation, bounded queues, and async diagnostics. |
| HTTP API runtime | ENGINE-04.A through ENGINE-04.C | One large-coherent HTTP PR for methods, headers/body limits, cancellation/backpressure, result serialization, and error contract. |
| SQLite and capabilities | ENGINE-05.A through ENGINE-05.C plus ENGINE-06.A | One large-coherent SQLite bridge PR so capability enforcement, cancellation-aware operations, cleanup, and users API conformance move together. |
| Lifecycle/source diagnostics | ENGINE-07.A, ENGINE-07.B, ENGINE-08.A | One or two PRs depending on source-map/V8 diagnostic blast radius. |
| Examples/evidence | ENGINE-09.A, ENGINE-09.B, ENGINE-10.A, ENGINE-10.B | One evidence PR after implementation layers pass, or split packaging if CI/package work is noisy. |
| Public alpha gate | ENGINE-11.A, ENGINE-11.B | Docs-only PR after Layers 1-9 pass. |

## Layer 0 - Cleanup / Truth Reset

Purpose: align tracker/docs/status with merged work and remove stale planning pressure.

EPICs: ENGINE-00.

Tasks:

- verify completed MAIN/MAIN1 parent issues were closed after review;
- verify stale old EPIC leftovers were closed or narrowed;
- keep only specific unfinished leftovers (#26 and #32 at cleanup time);
- verify benchmark/public-doc work is deferred;
- make docs point to the engine-foundation roadmap.

Prerequisites: live issue audit and reviewer approval. PR #256 applied the initial cleanup
after approval.

Dependencies: none.

Non-goals: no feature implementation, no new ENGINE issue creation before review.

Acceptance criteria: cleanup commands are reviewed/applied; only #26 and #32 remain open
from the old pre-ENGINE tracker; no duplicate ENGINE issues are created; public docs remain
blocked.

Likely PR grouping: one issue-cleanup/comment pass in the strategy PR, then one issue
creation pass after the dry-run is approved.

Parallelization: issue cleanup review can happen while Layer 1 contracts are drafted.

Risks: closing parent issues too aggressively can hide real remaining work; use comments
that point to replacement ENGINE issues.

## Layer 1 - Engine Contract Finalization

Purpose: lock final contracts before widening implementation.

EPICs: ENGINE-01.

Tasks:

- final JS app API;
- final Results API;
- final request context;
- final data/SQLite API;
- final async/Promise policy;
- final cancellation/deadline policy;
- final backpressure, queue, body, and resource-budget policy;
- final HTTP support matrix;
- decide which services/modules are core foundation versus later framework layer.

Prerequisites: Layer 0 truth reset or explicit decision to proceed with stale issues open.

Dependencies: compiler, runtime, HTTP, V8, data, diagnostics owners.

Non-goals: do not implement runtime/compiler/provider features in contract PRs unless a
task explicitly moves to an implementation layer.

Acceptance criteria: contracts define supported/deferred/rejected behavior and map to
tests, and every core async/HTTP/resource boundary has a cancellation, bounding, and
cleanup story.

Likely PR grouping: one contract doc PR, then focused follow-up PRs for compiler/runtime
contract alignment if needed.

Parallelization: HTTP, async, SQLite, and diagnostics contract sections can be drafted in
parallel, then reconciled in one final-shape review.

Risks: overfitting the Plan/API before examples validate ergonomics.

## Layer 2 - Compiler and Plan Completion

Purpose: compiler emits everything the runtime needs for realistic apps.

EPICs: ENGINE-02.

Tasks:

- async handler extraction;
- non-GET route methods;
- named handlers or documented rejection;
- SQLite/data API extraction needs;
- capability/provider Plan metadata emission;
- real source map output;
- deterministic Plan metadata/hashes;
- rejected-shape fixture matrix.

Prerequisites: Layer 1 contracts.

Dependencies: Plan schema, diagnostics, V8 module strategy, HTTP support matrix.

Non-goals: arbitrary JS bundler, npm resolution, Node compatibility.

Acceptance criteria: supported source examples compile to complete artifacts and rejected
source leaves no success artifacts.

Likely PR grouping: method extraction; async handler extraction; data/capability emission;
source maps; diagnostics matrix.

Parallelization: diagnostics fixture expansion and source-map work can proceed alongside
method/data extraction after the contracts freeze.

Risks: accidental support for dynamic JS shapes the runtime cannot validate.

## Layer 3 - V8 Runtime and Async Completion

Purpose: async handlers and V8 execution become real.

EPICs: ENGINE-03.

Tasks:

- Promise return support;
- microtask drain policy;
- owner-thread continuation dispatch;
- fulfilled/rejected Promise result conversion;
- async error diagnostics;
- request-scope retention until settlement;
- request cancellation propagation through JS and native completions;
- deadline/timeout hooks built on the cancellation path;
- bounded completion queues and explicit overflow diagnostics;
- app shutdown with pending async policy;
- V8-gated tests.

Prerequisites: Layer 1 async contract; enough Layer 2 compiler support for async fixtures.

Dependencies: `SlLoop`/`SlAsync`, V8 bridge, resource lifecycle, diagnostics.

Non-goals: broad public timers API, multi-worker scaling, production async DB offload
beyond the initial policy.

Acceptance criteria: async handler conformance covers fulfillment, rejection,
cancellation, cleanup, bounded queue overflow, and no `[object Promise]` fake success.

Likely PR grouping: bridge Promise detection/settlement; microtask policy; cancellation
token propagation; bounded queue/overflow diagnostics; request-scope retention;
diagnostics/conformance.

Parallelization: diagnostic expectations and conformance fixtures can be prepared while the
bridge mechanics are implemented.

Risks: V8 owner-thread violations, leaked request resources, false success for pending
Promises, and unbounded native work queues hidden behind an `async` facade.

## Layer 4 - HTTP Framework Runtime Completion

Purpose: HTTP supports realistic API building blocks.

EPICs: ENGINE-04.

Tasks:

- method support for GET/POST/PUT/PATCH/DELETE and decided OPTIONS policy;
- route precedence and ambiguity diagnostics;
- headers in request context;
- JSON body parsing;
- text body parsing;
- header/body size limits;
- request cancellation signal;
- read/body/handler timeout hooks;
- bounded request/response queues and backpressure policy;
- result serialization;
- error response contract;
- localhost server lifecycle conformance.

Prerequisites: Layer 1 HTTP contract; Layer 2 method metadata.

Dependencies: diagnostics, app host lifecycle, V8 async where body parsing can yield.

Non-goals: production-grade reverse proxy/server, TLS, HTTP/2, streaming upload/download,
multipart/file upload.

Acceptance criteria: realistic JSON API handlers execute through compiler/runtime
conformance with bounded body/header behavior, cancellation propagation, and explicit
backpressure/rejection behavior when limits are exceeded.

Likely PR grouping: method dispatch; headers/context; body parser/limits; result/errors;
server lifecycle and cancellation/backpressure tests.

Parallelization: response/error contract and parser/body-limit work can be split if tests
share fixtures carefully.

Risks: growing into production HTTP server scope before framework API runtime is solid.

## Layer 5 - SQLite End-to-End Foundation

Purpose: SQLite works from public JS handler through native provider.

EPICs: ENGINE-05.

Tasks:

- public JS SQLite API;
- capability-wired open/query/exec/queryOne;
- cancellation-aware operation boundaries;
- transaction policy;
- prepared-statement decision;
- app/request-scope cleanup;
- `:memory:` users API example;
- file DB policy decision.

Prerequisites: Layer 1 data contract; Layer 3 async policy; Layer 4 JSON HTTP runtime.

Dependencies: resource table, capability registry, V8 intrinsics, diagnostics.

Non-goals: ORM, migrations, PostgreSQL bridge, SQL Server bridge, production pooling.

Acceptance criteria: executable SQLite users API example passes conformance; denied
capability tests fail clearly; cancellation before/after sync-facade operations is
observable and documented.

Likely PR grouping: capability enforcement; transaction/prepared statement decision;
request/app cleanup; users API conformance.

Parallelization: docs/conformance can be drafted while bridge enforcement is implemented.

Risks: exposing file DB access without path/capability policy; blocking owner thread while
claiming scalable async.

## Layer 6 - Capability / Security Integration

Purpose: capabilities enforce real data access paths.

EPICs: ENGINE-06.

Tasks:

- wire SQLite bridge to native capability checks;
- denied diagnostics;
- audit/doctor real checks for Plan metadata;
- no OS sandbox overclaims;
- filesystem/network skeleton status cleanup.

Prerequisites: Layer 5 bridge shape or a narrow bridge hook.

Dependencies: Plan capabilities, resource lifecycle, diagnostics, CLI audit.

Non-goals: OS sandbox, permission prompts, filesystem/network APIs.

Acceptance criteria: denied SQLite access cannot reach provider work; docs and tests prove
the enforcement point.

Likely PR grouping: bridge enforcement; diagnostics/audit; docs/conformance.

Parallelization: audit/docs work can proceed after enforcement API is stable.

Risks: metadata-only checks being mistaken for runtime security.

## Layer 7 - App-Host Lifecycle / Resource Completion

Purpose: request/app scopes, cleanup, shutdown, and diagnostics are solid.

EPICs: ENGINE-07, ENGINE-08.

Tasks:

- app startup/shutdown hooks;
- request scope lifecycle;
- cancellation token/signal lifecycle;
- resource cleanup ordering;
- provider handle ownership;
- bounded resource budgets;
- pending async shutdown policy;
- graceful drain versus force-cancel policy;
- lifecycle diagnostics;
- leak checks where possible.
- runtime/compiler/V8 source-map diagnostics needed to make cleanup and failure reports
  source-aware.

Prerequisites: Layers 3-6 define async/resources/provider behavior.

Dependencies: V8 bridge, `SlScope`, resource table, SQLite bridge, HTTP lifecycle.

Non-goals: broad DI container, plugin lifecycle, multi-worker process manager.

Acceptance criteria: cleanup happens on success, handler error, rejected Promise,
cancelled request, unsupported request, server stop, graceful drain timeout, and app
shutdown.

Likely PR grouping: request scope; app scope; shutdown diagnostics; source-map remapping;
async diagnostic JSON surfaces; leak checks.

Parallelization: request-scope and app-scope tests can be prepared independently after
resource ownership is specified. Source-map/diagnostic work can proceed in parallel once
compiler maps and V8 exception surfaces are stable.

Risks: resource leaks hidden by process exit; cleanup order tied to accidental implementation.

## Layer 8 - End-to-End Examples / Conformance

Purpose: prove realistic apps work.

EPICs: ENGINE-09.

Tasks:

- hello;
- request context;
- async handler;
- cancellation/abort cleanup;
- JSON body;
- SQLite users API;
- denied capability;
- unsupported behavior.

Prerequisites: Layers 2-7.

Dependencies: compiler, V8, HTTP, SQLite, capabilities, packaging.

Non-goals: public tutorial polish, large sample app suite, benchmark scenarios.

Acceptance criteria: every foundation example has default compile/reject evidence and
V8-gated run evidence where execution is claimed.

Likely PR grouping: conformance harness expansion; example set; docs evidence table.

Parallelization: unsupported cases and positive examples can be split once shared harness
helpers exist.

Risks: examples drifting away from compiler-supported source.

## Layer 9 - Packaging / Evidence

Purpose: prove packaged local runtime works.

EPICs: ENGINE-10.

Tasks:

- V8-gated conformance gate;
- package smoke outside checkout;
- packaged `sloppyc build`;
- packaged `sloppy run --artifacts`;
- stdlib/V8 runtime file validation;
- evidence report template;
- evidence for cancellation, resource limits, and backpressure paths.

Prerequisites: Layer 8 examples.

Dependencies: package scripts, CI, V8 SDK strategy, docs evidence model.

Non-goals: installers, signing/notarization, package registries, public release automation.

Acceptance criteria: packaged local runtime executes foundation examples outside the
checkout and reports default/V8/package/provider evidence separately.

Likely PR grouping: package run smoke; CI/evidence reporting; docs.

Parallelization: package script improvements and evidence docs can proceed in parallel.

Risks: package layout smoke being mistaken for V8 execution proof.

## Layer 10 - Public Alpha Docs Gate

Purpose: public docs only after Layers 1-9.

EPICs: ENGINE-11.

Tasks:

- alpha readiness checklist;
- executable getting-started docs;
- SQLite tutorial only after conformance;
- denied capability docs;
- troubleshooting for V8/package evidence;
- public non-claims review.

Prerequisites: Layers 1-9 accepted or explicitly deferred with honest language.

Dependencies: conformance/evidence report, README/public docs, quality score.

Non-goals: launch/marketing pages, competitor claims, benchmark marketing.

Acceptance criteria: public docs describe only executable verified workflows or explicit
deferred behavior.

Likely PR grouping: checklist first, then public docs refresh.

Parallelization: documentation can be drafted earlier but must stay blocked until evidence
passes.

Risks: publishing aspirational docs before examples execute.
