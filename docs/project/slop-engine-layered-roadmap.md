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
| V8 bounded Promise runtime | ENGINE-03.A through ENGINE-03.D plus ENGINE-08.B | One large-coherent V8 async PR covering returned-Promise settlement, owner-thread microtasks, cancellation snapshots, bounded pending-Promise failure, cleanup, and async diagnostics. |
| Scalable async runtime | ENGINE-12.A through ENGINE-12.D | One large-coherent async-runtime PR only when native completions/provider offload/deadline/shutdown work is ready to cross the runtime boundary. |
| Proper HTTP backend | ENGINE-13.A through ENGINE-13.F | One or more backend PRs for listener ownership, connection/request lifecycle, parser/body limits, cancellation, backpressure, graceful shutdown, diagnostics, and stress/conformance smoke. |
| Module/bootstrap completion | ENGINE-14.A through ENGINE-14.E | One coherent V8/bootstrap PR sequence for stdlib asset loading, app module loading, ESM/classic decision, cache policy, intrinsic boundaries, source names, and diagnostics. |
| Diagnostics/source maps | ENGINE-15.A through ENGINE-15.E | One or two PRs for source-map completion, V8 remapping, async diagnostic JSON/source frames, redaction, stable codes, and diagnostic goldens. |
| App/resource lifetime | ENGINE-16.A through ENGINE-16.E | One lifecycle PR sequence for app startup/shutdown, request/app scopes, cleanup on success/error/cancel, leak-oriented hooks, and lifecycle diagnostics. |
| SQLite completion | ENGINE-17.A through ENGINE-17.E | One large-coherent SQLite PR when JS API finalization, capability-wired open/use, transactions, result mapping, cleanup, and users API proof can move together. |
| CLI/dev loop runtime | ENGINE-18.A through ENGINE-18.E | One or more tooling PRs for build/run UX, artifact inspection, source-input decision, doctor/audit checks, OpenAPI skeleton policy, and dev/watch decision. |
| Conformance compatibility | ENGINE-19.A through ENGINE-19.E | One evidence PR sequence for default/V8-gated/runtime/package conformance once implementation layers exist. |
| Strong Plan layer | ENGINE-20.A through ENGINE-20.E | One strategic Plan PR sequence for typed route/handler/capability/provider/artifact graphs, validation, compatibility, audit/doctor, and future hooks. |
| Memory/string foundations | ENGINE-21.A through ENGINE-21.F | One primitive-foundation PR sequence for lifetime/allocation policy, owned string/byte views, builders, bounded string interning/symbol tables, V8/SQLite conversion policy, and safety/stress evidence. |
| Memory/string adoption | ENGINE-22.A through ENGINE-22.F | One adoption PR sequence for HTTP, diagnostics/CLI, Plan/artifact, V8, SQLite, and hot-path cleanup after ENGINE-21 primitives land. |
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

ENGINE-01 contract source: `docs/project/engine-framework-contract.md`. Implementation
layers should treat that document as the decision record for JS app shape, Results,
request context, async/microtasks, cancellation/deadlines, limits/backpressure, HTTP,
SQLite, capabilities, and deferred behavior.

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

Likely PR grouping: one contract doc PR, then mid-sized bounded-context implementation PRs
in later layers. Follow-up PRs should group related tasks by module or contract boundary
when that keeps development faster without making review blurry.

Parallelization: HTTP, async, SQLite, and diagnostics contract sections can be drafted in
parallel, then reconciled in one final-shape review.

Risks: overfitting the Plan/API before examples validate ergonomics.

## Layer 2 - Compiler and Plan Completion

Purpose: compiler emits everything the runtime needs for realistic apps.

EPICs: ENGINE-02.

Status: ENGINE-02 compiler/Plan PR covers the first large-coherent slice: supported
method extraction, async-handler metadata emission, minimal SQLite capability/provider
metadata, deterministic artifacts, source-map artifacts, and rejected-shape fixtures.
Source-input `sloppy run app.js`, module/service graph extraction, and runtime execution of
async/non-GET/provider behavior remain later layers.

Tasks:

- async handler extraction metadata;
- non-GET route method metadata;
- named handlers or documented rejection;
- minimal SQLite/data capability extraction needs;
- capability/provider Plan metadata emission;
- real handler-line source map output;
- deterministic Plan metadata/hashes;
- #302 source-input `sloppy run app.js` compiler/CLI handoff, cache key, stale-artifact,
  diagnostics, and cleanup policy;
- rejected-shape fixture matrix.

Prerequisites: Layer 1 contracts.

Dependencies: Plan schema, diagnostics, V8 module strategy, HTTP support matrix.

Non-goals: arbitrary JS bundler, npm resolution, Node compatibility.

Acceptance criteria: supported source examples compile to complete artifacts and rejected
source leaves no success artifacts.

Likely PR grouping: ENGINE-02 compiler/Plan slice first; source-input handoff/cache remains
separate; broader module/service extraction remains separate.

Parallelization: diagnostics fixture expansion and source-map work can proceed alongside
method/data extraction after the contracts freeze.

Risks: accidental support for dynamic JS shapes the runtime cannot validate.

## Layer 3 - V8 Runtime and Bounded Promise Completion

Purpose: async handlers and V8 execution become real.

EPICs: ENGINE-03.

Current implementation note: ENGINE-03 now has a V8-gated microtask-only async handler
slice. Returned handler Promises that settle during the explicit owner-thread microtask
drain fulfill/reject deterministically, pending Promises fail as deadline-style handler
failures, cancellation/deadline/backpressure have a native token snapshot, and cleanup is
covered for the bounded call path. Native async provider queues, public timer/fetch APIs,
HTTP disconnect/shutdown drain behavior, stress/performance evidence, and broader compiler
async source shapes remain future work.

Tasks:

- Promise return support;
- microtask drain policy;
- owner-thread continuation dispatch;
- fulfilled/rejected Promise result conversion;
- async error diagnostics;
- request-scope retention until settlement;
- request cancellation snapshots through the bounded handler call;
- pending-Promise failure through the bounded deadline-style path;
- app/request cleanup for success, rejection, cancellation, and bounded pending-Promise
  failure;
- compiler follow-through that reopens `SLOPPYC_E_UNSUPPORTED_ASYNC_HANDLER_BODY` and
  graduates `await`, multi-statement async bodies, and non-direct async returns only after
  the runtime Promise policy is executable;
- V8-gated tests.

Prerequisites: Layer 1 async contract; enough Layer 2 compiler support for async fixtures.

Dependencies: `SlLoop`/`SlAsync`, V8 bridge, resource lifecycle, diagnostics.

Non-goals: broad public timers API, native async provider queues, multi-worker scaling,
production async DB offload, stress/performance claims, or full event-loop behavior.

Acceptance criteria: async handler conformance covers fulfillment, rejection,
cancellation snapshot behavior, cleanup, bounded pending-Promise failure, compiler
acceptance only for newly executable async source shapes, and no `[object Promise]` fake
success.

Likely PR grouping: bridge Promise detection/settlement; microtask policy; cancellation
snapshot; bounded pending-Promise diagnostics; request-scope cleanup;
diagnostics/conformance.

Parallelization: diagnostic expectations and conformance fixtures can be prepared while the
bridge mechanics are implemented.

Risks: V8 owner-thread violations, leaked request resources, false success for pending
Promises, and accidentally documenting microtask-only async as scalable async.

## Layer 3B - Scalable Async Runtime

Purpose: make Sloppy's full async runtime real when native async work exists.

EPICs: ENGINE-12 (#306).

This layer is the explicit target for "full async runtime with all shenanigans": native
completion queues/backends, owner-thread V8 continuation scheduling for native
completions, cancellation/deadline/shutdown drain behavior, bounded queues and
backpressure, provider/offload integration, cleanup-once behavior across queued work, and
stress evidence. It is required before Sloppy claims scalable async performance,
production-ready async HTTP lifecycle, async provider execution, or benchmark results for
many pending requests.

Implement when:

- a real external async source is ready to cross the runtime boundary, such as HTTP
  disconnect/shutdown cancellation, timer/deadline wakeups, async SQLite/provider work, or
  worker-pool offload;
- ENGINE-03's bounded Promise/microtask semantics are stable enough to preserve while
  adding native completions;
- request/app scope ownership is defined well enough to retain and release resources across
  queued async work;
- public alpha docs, packaged evidence, benchmark methodology, or product language would
  otherwise imply scalable async behavior.

Tasks:

- #307 / TASK ENGINE-12.A: native event loop and completion queue backend;
- #308 / TASK ENGINE-12.B: owner-thread V8 continuation scheduler;
- #309 / TASK ENGINE-12.C: cancellation, deadline, and shutdown drain policy;
- #310 / TASK ENGINE-12.D: async backpressure, provider offload, and scalability evidence.

Prerequisites: ENGINE-03 accepted; native loop/worker/provider boundaries ready for a real
async source; request-scope cleanup contract clear enough to survive pending work.

Dependencies: `SlLoop`/`SlAsync`, V8 bridge, app/request lifecycle, diagnostics, HTTP
shutdown/disconnect policy, provider/offload strategy, testing/evidence harness.

Non-goals: Node compatibility, npm/package-manager behavior, public timers/fetch/fs/process
APIs unless explicitly scoped, broad multi-isolate scaling, PostgreSQL/SQL Server JS bridge
expansion before SQLite/core async evidence is solid, and external benchmark comparisons.

Acceptance criteria: native completions resume JavaScript only on the owning V8 thread;
queue capacity and overflow are documented/tested; cancellation/deadline/shutdown reasons
produce deterministic diagnostics; cleanup runs exactly once on success, rejection,
cancellation, timeout, overflow, and shutdown; stress evidence shows many pending
operations do not become thread-per-request behavior; default, V8-gated, live-provider,
stress, and benchmark evidence are reported separately.

Likely PR grouping: one large-coherent async runtime PR if the completion backend,
owner-thread scheduler, cancellation/deadline policy, and first realistic async source can
be reviewed together. Split only if backend ownership or provider/offload evidence becomes
too large to validate in one pass.

Parallelization: diagnostics and stress/evidence fixtures can be prepared while the
backend/scheduler API is implemented, but V8 continuation tests must wait for a real
owner-thread dispatch path.

Risks: unbounded queues hidden behind async APIs, worker/provider threads entering V8,
request-scope leaks across pending work, shutdown hangs, and benchmark-looking smoke tests
being mistaken for scalability proof.

## Layer 3C - Remaining Engine Foundation Completion

Purpose: finish the rest of Sloppy's engine foundation after ENGINE-12 without turning the
project into Node/npm compatibility, a production internet-edge server, ORM/migration
stack, public alpha launch, or benchmark marketing project.

Strategic source: `docs/project/engine-13-plus-architecture.md`.

EPICs: ENGINE-13 through ENGINE-20, plus the cross-cutting memory/string foundation in
ENGINE-21 and ENGINE-22.

Proper async and proper HTTP remain separate layers. ENGINE-12 owns the generic native
completion, owner-thread continuation, cancellation/deadline/shutdown, queue, provider
offload, and stress-evidence backend. ENGINE-13 owns HTTP-specific listener, connection,
request, parser, body, keep-alive, timeout, backpressure, graceful shutdown, and server
diagnostic policy on top of those primitives.

Tasks by EPIC:

- ENGINE-13: proper HTTP runtime backend for listener/backend architecture,
  connection/request lifecycle, parser/body limits, cancellation, backpressure, graceful
  shutdown, diagnostics, and stress/conformance smoke.
- ENGINE-14: module loading and runtime bootstrap completion for stdlib/bootstrap asset
  loading, app module loading, ESM/classic decision, module cache, import rewrite and
  intrinsic boundaries, source names, and startup diagnostics.
- ENGINE-15: source maps and diagnostics completion across compiler, generated artifacts,
  V8 exceptions, async errors, JSON diagnostics, redaction, source frames, CLI format, and
  goldens.
- ENGINE-16: app host and resource lifetime runtime for startup/shutdown, app/request
  scopes, resource cleanup hooks, cancellation propagation, leak-oriented hooks, and
  lifecycle diagnostics.
- ENGINE-17: SQLite runtime and data access completion for public JS API, native bridge,
  capability-wired open/use, query/exec/queryOne, transactions, prepared statement
  decision, result mapping, file and memory policy, cleanup, cancellation, and users API
  proof.
- ENGINE-18: CLI and dev loop runtime for `sloppyc`/`sloppy run` UX, source-input run
  decision, artifact inspection, doctor, audit, OpenAPI route skeleton policy, optional
  watch/rebuild, and command diagnostics.
- ENGINE-19: conformance harness and runtime compatibility suite for compiler to Plan to
  runtime to V8 to HTTP behavior, async, HTTP body/header cases, SQLite, capability
  denial, lifecycle cleanup, package outside-checkout smoke, and explicit default versus
  optional gates.
- ENGINE-20: strong Plan strategic layer for typed route, handler, capability, provider,
  artifact graphs, static validation, future OpenAPI/optimization/policy/audit hooks,
  versioning, compatibility, and internal tooling leverage.
- ENGINE-21: memory and string runtime foundations for app/request/temp lifetimes,
  allocation policy, string/byte views, owned string/buffer rules, builders, formatting,
  bounded string interning/symbol tables, V8/SQLite interop policy, and safety/stress
  evidence.
- ENGINE-22: memory/string adoption and hot-path refactor for HTTP parse/write, V8
  conversions, SQLite result/parameter mapping, diagnostics/source frames/JSON, Plan and
  artifact loading, CLI output, and conformance/benchmark guards.

Prerequisites: ENGINE-01 contract, ENGINE-02/03 implementation evidence, ENGINE-12 when
native async completion behavior is required, and the current issue index in
`docs/project/engine-13-plus-issue-index.md`.

Dependencies: HTTP, V8, core lifecycle/resource, data/SQLite, compiler, Plan, diagnostics,
CLI, tests/conformance, package tooling, memory/string primitives, and quality gates.

Non-goals: Node compatibility, npm/package-manager behavior, production reverse proxy or
internet-edge claims, TLS/HTTP2/HTTP3/WebSockets/static file/compression work unless
separately scoped, ORM/migrations, PostgreSQL/SQL Server JS bridge expansion, benchmark
claims, and public alpha docs.

Acceptance criteria: each EPIC has issue-backed tasks, implementation PRs update source
docs and tests together, public alpha remains blocked until the foundation evidence gate
passes, PostgreSQL/SQL Server stay deferred, benchmark output stays non-claim evidence, and
hot-path memory/string adoption is complete or explicitly deferred with honest evidence.

Likely PR grouping: prefer bounded but coherent implementation PRs by ownership surface.
Do not split one lifecycle or backend invariant across tiny PRs if that would leave fake
success or untested cleanup paths.

Parallelization: docs, diagnostics goldens, issue metadata, and conformance fixture
planning can proceed while runtime owners work. Runtime behavior that crosses lifecycle,
async, V8, or provider boundaries should wait for the dependent contracts to be stable.

## Layer 3D - Memory/String Foundation And Adoption

Purpose: make memory and string handling a deliberate engine foundation rather than a set
of local helpers.

Strategic sources:

- `docs/project/memory-string-current-state-audit.md`;
- `docs/project/memory-string-foundation-architecture.md`;
- `docs/project/memory-string-adoption-map.md`.
- `docs/project/engine-21-22-issue-index.md`.

EPICs: ENGINE-21 and ENGINE-22.

ENGINE-21 is primitive work. It defines app/request/temp/static/V8/SQLite/diagnostic
lifetimes, allocation and failure policy, string and byte views, owned strings/buffers,
byte and string builders, formatting helpers, bounded app/static string interning and
symbol tables, V8/native conversion policy, SQLite text/blob ownership, and memory
safety/stress tests.

ENGINE-22 is adoption work. It migrates HTTP parser/request/response paths, V8 bridge
conversions, SQLite row/result/parameter conversion, diagnostics/source-frame/JSON
formatting, Plan/artifact loading, CLI output, and allocation-aware conformance/benchmark
guards after ENGINE-21 primitives land.

Prerequisites: existing `SlStr`, `SlBytes`, `SlArena`, checked math, diagnostics,
resource-table, HTTP, V8, SQLite, Plan, and CLI foundations.

Dependencies: ENGINE-13 HTTP backend, ENGINE-14 module/bootstrap, ENGINE-15 diagnostics,
ENGINE-16 lifecycle, ENGINE-17 SQLite, ENGINE-19 conformance, and ENGINE-20 Plan.

Non-goals: general-purpose STL clone, complex allocator framework, lock-free allocator,
full Unicode library, JSON DOM library, ORM/migrations, Node Buffer compatibility,
package-manager behavior, or broad runtime/compiler/provider refactors in the architecture
PR.

Acceptance criteria: the primitive contracts are documented and issue-backed; old #32
string/buffer work is absorbed or superseded; adoption tasks identify hot paths and
dependency order; public alpha docs remain blocked until memory/string adoption and
conformance evidence pass or are honestly scoped down.

Likely PR grouping: one docs/issue roadmap PR; one or more ENGINE-21 primitive PRs; then
ENGINE-22 adoption PRs by subsystem.

Parallelization: ENGINE-21.A/B/C can be designed together. ENGINE-21.F should wait for
hash/equality and lifetime contracts from ENGINE-21.B/A before implementation. ENGINE-22
subsystem adoption should wait for builder, ownership, and symbol-table contracts. V8 and
SQLite bridge adoption should not edit the same intrinsic bridge in parallel without a
shared owner.

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

Prerequisites: Layers 1-9 accepted or explicitly deferred with honest language. ENGINE-12
must also be accepted before public docs claim scalable async runtime behavior, async
provider scalability, production-ready async HTTP lifecycle, or performance with many
pending requests.

Dependencies: conformance/evidence report, README/public docs, quality score.

Non-goals: launch/marketing pages, competitor claims, benchmark marketing.

Acceptance criteria: public docs describe only executable verified workflows or explicit
deferred behavior.

Likely PR grouping: checklist first, then public docs refresh.

Parallelization: documentation can be drafted earlier but must stay blocked until evidence
passes.

Risks: publishing aspirational docs before examples execute.
