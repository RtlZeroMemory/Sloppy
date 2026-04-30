# ENGINE-13+ Architecture Roadmap

Status: strategic architecture and issue planning source.

This document defines the remaining Slop Engine foundation EPICs after ENGINE-12. It is a
planning document only: it does not implement runtime, compiler, provider, HTTP, or CLI
behavior, and it does not create public alpha claims.

ENGINE-12 owns the scalable async backend: native completion queues, owner-thread V8
continuation scheduling, cancellation/deadline/shutdown drain behavior, bounded async
backpressure, provider/offload integration, and stress evidence. ENGINE-13 and later cover
the rest of the foundation needed before Sloppy can honestly expand into public alpha
docs or higher-level userland framework work.

The target is Sloppy's own engine foundation, not Node compatibility, npm/package-manager
behavior, production internet-edge server claims, ORM/migrations, PostgreSQL/SQL Server JS
bridge expansion, benchmark marketing, or public alpha documentation.

## Strategic Decisions

- Proper async and proper HTTP are separate layers. ENGINE-12 defines how native async
  work completes and resumes JavaScript safely. ENGINE-13 defines how HTTP connections,
  requests, bodies, deadlines, backpressure, shutdown, and diagnostics are represented by
  the server backend. HTTP uses async primitives, but it must also make parser, lifecycle,
  body, and connection policy decisions that are not part of a generic async runtime.
- The new EPICs intentionally overlap older closed EPIC numbers from the pre-ENGINE
  roadmap. The canonical issue titles use `ENGINE-13` through `ENGINE-20` to distinguish
  this foundation-completion batch from the old `EPIC-13` style tracker.
- PostgreSQL and SQL Server remain deferred from this foundation batch. Native provider
  boundaries can remain, but public JS bridge expansion waits until SQLite is complete and
  capability-wired.
- Benchmarks remain local/manual evidence until the actual engine paths, methodology,
  build configuration, hardware context, and trend policy are stable. No benchmark claim
  is introduced by these EPICs.
- Public alpha docs stay blocked until the foundation evidence gates prove the supported
  compiler, Plan, V8, HTTP, SQLite, lifecycle, CLI, and conformance paths.

## Shared Acceptance Gates

Every implementation PR under ENGINE-13+ should:

- cite the relevant section of this document and the current framework contract;
- keep V8 types inside `src/engine/v8/*`;
- keep OS APIs behind platform modules;
- keep JS free of raw native pointers;
- document implemented versus deferred behavior in the same PR;
- add unit, integration, conformance, or golden coverage for the documented behavior;
- report default, V8-gated, package, live-provider, stress, and benchmark evidence
  separately when relevant;
- avoid public alpha docs and marketing claims.

## ENGINE-13: Proper HTTP Runtime Backend

Goal: build the real HTTP serving backend foundation underneath framework semantics.

Why it exists: Sloppy currently has useful HTTP parser, route, response, and dev-only run
pieces, but a proper backend must own connection lifecycle, request lifecycle, bounded
buffers, body policy, deadlines, cancellation, backpressure, shutdown, and server
diagnostics as a coherent host layer.

Dependencies:

- ENGINE-12 for scalable async completion and shutdown/cancellation primitives when native
  HTTP work crosses async boundaries;
- ENGINE-16 for app/request scope ownership and cleanup hooks;
- ENGINE-15 for stable diagnostics and source-aware error reporting;
- Plan route metadata from ENGINE-20 where startup validation needs typed graphs.

Task breakdown:

- TASK ENGINE-13.A: HTTP Backend Architecture and Platform Boundary;
- TASK ENGINE-13.B: Connection and Request Lifecycle;
- TASK ENGINE-13.C: Parser Limits, Timeouts, and Backpressure;
- TASK ENGINE-13.D: Body Reader and Cancellation Integration;
- TASK ENGINE-13.E: Graceful Shutdown and Server Diagnostics;
- TASK ENGINE-13.F: HTTP Backend Stress and Conformance Smoke.

Implementation risks:

- accidentally implying production-grade internet-edge server behavior;
- hiding unbounded body/header/queue allocation behind a friendly API;
- mixing platform socket details into core modules;
- running V8 or app cleanup from the wrong thread;
- making keep-alive or shutdown behavior depend on accidental parser state.

Acceptance criteria:

- listener/backend ownership and platform boundary are documented and tested;
- connection and request states are explicit and deterministic;
- parser, target, header, body, response, and queue limits reject with diagnostics;
- deadlines and cancellation integrate with the shared request token;
- graceful shutdown drains or cancels through a bounded policy;
- server diagnostics identify bind, parse, body, handler, cancellation, timeout, and
  backpressure failures;
- stress/conformance smoke proves bounded behavior without benchmark claims.

Non-goals:

- TLS;
- HTTP/2;
- HTTP/3;
- reverse proxy behavior;
- WebSockets;
- static file server;
- compression;
- production benchmark claims.

Suggested PR grouping:

- architecture/platform boundary plus state model;
- parser limits, body buffering, deadlines, and backpressure;
- graceful shutdown, diagnostics, and conformance/stress smoke.

Parallelization notes: diagnostics goldens and conformance fixtures can be drafted while
backend state APIs are designed. Body reader work should wait until request lifecycle and
cancellation ownership are stable.

## ENGINE-14: Module Loading and Runtime Bootstrap Completion

Goal: make V8, stdlib bootstrap, and app module loading a real stable runtime layer.

Why it exists: the current V8 path uses a classic bootstrap asset and generated app
artifact handoff. That is useful but not a final module-loading story. Sloppy needs a
documented bootstrap loader, source naming, cache policy, intrinsic boundary, and clear
ESM-versus-classic decision before public examples can rely on module behavior.

Dependencies:

- ENGINE-15 for source names and diagnostics;
- ENGINE-16 for app startup/shutdown ownership;
- ENGINE-20 for artifact graph and compatibility metadata;
- existing V8 owner-thread and intrinsic boundary rules from ENGINE-03.

Task breakdown:

- TASK ENGINE-14.A: Bootstrap Asset Loading Contract;
- TASK ENGINE-14.B: App Module Loading and Cache Policy;
- TASK ENGINE-14.C: ESM vs Classic Runtime Decision;
- TASK ENGINE-14.D: Intrinsic Boundary and Import Rewrite Contract;
- TASK ENGINE-14.E: Module Loading Diagnostics and Tests.

Implementation risks:

- accidentally creating Node/npm resolution semantics;
- making generated classic scripts and future ESM modules disagree on bootstrap API;
- hiding global mutable state in module caches;
- losing source names needed by diagnostics;
- placing provider intrinsics directly in `engine_v8.cc`.

Acceptance criteria:

- bootstrap asset discovery, hashing, staging, and failure diagnostics are stable;
- app module loading and cache behavior are documented;
- the ESM-versus-classic policy is explicit and test-backed;
- import rewriting boundaries stay compiler-owned, not npm-like;
- V8 startup/module diagnostics include source names and safe hints;
- intrinsic registration remains isolated and provider-specific bridges stay in sibling
  `intrinsics_<provider>.cc` files.

Non-goals:

- npm;
- Node module resolution;
- package manager behavior;
- bundler ecosystem behavior;
- dynamic remote imports.

Suggested PR grouping:

- bootstrap asset loading and diagnostics;
- app module/cache policy and ESM/classic decision;
- intrinsic/import rewrite contract plus tests.

Parallelization notes: compiler import-rewrite docs and V8 source-name diagnostics can be
prepared in parallel, but cache semantics should land with one runtime implementation
choice.

## ENGINE-15: Source Maps and Diagnostics Completion

Goal: make compiler/runtime/V8 diagnostics source-aware, safe, stable, and
machine-readable.

Why it exists: diagnostics are a product surface. Sloppy has stable diagnostic primitives,
compiler source-map artifacts, JSON rendering, redaction, and V8 generated-source errors,
but the foundation still needs author-source remapping, async diagnostic JSON/source
frames, CLI format consistency, and golden coverage across compiler/runtime boundaries.

Dependencies:

- ENGINE-02 source-map artifacts and compiler diagnostic data;
- ENGINE-03/ENGINE-12 async and V8 rejection semantics;
- ENGINE-14 source names and module-loading policy;
- ENGINE-18 CLI format plumbing;
- ENGINE-19 conformance/golden harness.

Task breakdown:

- TASK ENGINE-15.A: Compiler Source Map Completion;
- TASK ENGINE-15.B: V8 Exception Source Remapping;
- TASK ENGINE-15.C: Async Diagnostic JSON and Source Frames;
- TASK ENGINE-15.D: Redaction and Stable Diagnostic Codes;
- TASK ENGINE-15.E: Diagnostic Golden Suite.

Implementation risks:

- claiming author-source fidelity from generated-source spans;
- leaking secrets through source frames, provider details, or JSON diagnostics;
- producing unstable machine-readable output;
- creating separate compiler/runtime diagnostic formats that cannot be tested together;
- treating async stacks as solved before the runtime has enough information.

Acceptance criteria:

- compiler source maps contain the mappings needed by runtime diagnostics;
- V8 exceptions and Promise rejections remap to author source where claimed;
- JSON diagnostics are deterministic, redacted, and versionable;
- CLI error output shares stable formatting and code names;
- diagnostic goldens cover compiler, Plan, V8, async, HTTP, SQLite, and capability denial
  paths as they become implemented.

Non-goals:

- full IDE language server;
- source-map support beyond generated Sloppy artifacts;
- arbitrary bundler source maps.

Suggested PR grouping:

- compiler/source-map completion;
- V8 remapping and async diagnostic JSON/source frames;
- redaction/code audit and golden suite expansion.

Parallelization notes: golden fixtures and redaction review can start before remapping
lands. CLI output work should wait for shared renderer decisions.

## ENGINE-16: App Host and Resource Lifetime Runtime

Goal: make app/request lifecycle deterministic across success, error, async,
cancellation, and shutdown.

Why it exists: Sloppy now has scoped primitives, a resource table, request cleanup smoke,
and V8-gated resource IDs, but public framework behavior needs a real app startup/shutdown
model, app/request scopes, ownership rules, cleanup hooks, cancellation propagation, and
lifecycle diagnostics.

Dependencies:

- ENGINE-12 for pending async work and shutdown drain policy;
- ENGINE-13 for HTTP request lifecycle events;
- ENGINE-17 for SQLite resource ownership;
- ENGINE-20 for app/resource graph metadata;
- existing `SlScope`, `SlResourceTable`, and cancellation primitives.

Task breakdown:

- TASK ENGINE-16.A: App Startup and Shutdown Lifecycle;
- TASK ENGINE-16.B: Request Scope and App Scope Ownership;
- TASK ENGINE-16.C: Resource Cleanup on Success/Error/Cancel;
- TASK ENGINE-16.D: Leak-Oriented Test Hooks;
- TASK ENGINE-16.E: Lifecycle Diagnostics.

Implementation risks:

- cleanup paths that only work on happy-path completion;
- request resources that outlive their scope without diagnostics;
- app shutdown that races pending async completions;
- hidden global mutable lifecycle state;
- debug-only leak detection being mistaken for release guarantees.

Acceptance criteria:

- app startup failure, successful startup, graceful shutdown, and forced shutdown have
  deterministic states;
- app-scope and request-scope resources have clear ownership and close order;
- cleanup runs exactly once on success, sync error, Promise rejection, cancellation,
  timeout, overflow, and shutdown where scoped;
- leak-oriented hooks catch unclosed resources where practical;
- lifecycle diagnostics identify the owning app/request/resource boundary.

Non-goals:

- DI container;
- plugin runtime;
- production process manager;
- distributed lifecycle.

Suggested PR grouping:

- app startup/shutdown state machine;
- request/app scope ownership and cleanup hooks;
- leak-oriented tests and lifecycle diagnostics.

Parallelization notes: diagnostic codes and leak-test design can proceed while the state
machine is drafted. SQLite/app resource work should not land before ownership rules are
stable.

## ENGINE-17: SQLite Runtime and Data Access Completion

Goal: make SQLite the first complete data proof for Slop.

Why it exists: SQLite is the right foundation database story because it can be tested
locally, packaged, capability-wired, and used by realistic examples without external
services. The native provider and V8 bridge exist, but the public JS API, capability
enforcement, transactions/prepared-statement decision, result mapping, file policy,
request/app ownership, and users API proof still need completion.

Dependencies:

- ENGINE-16 for resource lifetime ownership;
- ENGINE-15 for data diagnostics and redaction;
- ENGINE-19 for conformance examples;
- ENGINE-20 for provider/capability graph metadata;
- ENGINE-12 only if SQLite work moves beyond sync-backed owner-thread operations.

Task breakdown:

- TASK ENGINE-17.A: SQLite Public JS API Finalization;
- TASK ENGINE-17.B: SQLite Capability-Wired Open/Use;
- TASK ENGINE-17.C: SQLite Transactions and Prepared Statement Decision;
- TASK ENGINE-17.D: SQLite Result Mapping and Error Policy;
- TASK ENGINE-17.E: SQLite Users API Runtime Proof.

Implementation risks:

- exposing native provider details or raw pointers to JavaScript;
- blessing file database access before path/capability policy is enforceable;
- describing sync-backed operations as scalable async;
- letting capability metadata remain advisory rather than enforced;
- leaking connections/statements across request/app scopes.

Acceptance criteria:

- public JS API covers `open`, `exec`, `query`, `queryOne`, `transaction`, and `close`
  according to the final contract;
- SQLite open/use checks capabilities before provider work;
- `:memory:` policy is executable and file database policy is explicit;
- result rows and errors map deterministically;
- transaction and prepared-statement policy is documented and tested;
- resource cleanup works through app/request scope;
- a users API example runs through the real supported path.

Non-goals:

- ORM;
- migrations;
- PostgreSQL;
- SQL Server;
- connection pool framework beyond SQLite need.

Suggested PR grouping:

- JS API finalization and capability enforcement;
- transactions/prepared statement decision plus result/error mapping;
- users API runtime proof and cleanup/conformance.

Parallelization notes: docs and conformance fixtures can be prepared early. Capability
enforcement and resource cleanup should land in the same coherent implementation window.

## ENGINE-18: CLI and Dev Loop Runtime

Goal: make `sloppyc`, `sloppy run`, inspection, doctor, and audit usable for engine
development.

Why it exists: Sloppy already has useful metadata inspection commands and explicit
artifact execution. The foundation still needs cohesive command UX, source-input run
decision handling, artifact inspection, real doctor/audit checks, OpenAPI skeleton policy,
optional dev/watch decisions, and clear diagnostics for failed build/run flows.

Dependencies:

- ENGINE-14 for module/bootstrap loading behavior;
- ENGINE-15 for CLI diagnostic formats;
- ENGINE-19 for evidence/conformance status;
- ENGINE-20 for Plan-driven inspection and audit data;
- #302 for direct source-input run handoff if selected.

Task breakdown:

- TASK ENGINE-18.A: sloppyc Build UX and Artifact Inspection;
- TASK ENGINE-18.B: sloppy Run UX and Source-Input Run Decision;
- TASK ENGINE-18.C: Doctor and Audit Real Artifact Checks;
- TASK ENGINE-18.D: OpenAPI Route Skeleton Policy;
- TASK ENGINE-18.E: Dev Loop / Watch Decision.

Implementation risks:

- making `sloppy run app.js` look implemented before a real compiler handoff exists;
- turning doctor/audit into live provider or V8 proof by implication;
- over-promising OpenAPI beyond route skeleton metadata;
- adding watch/dev loop behavior without cleanup and stale-artifact policy;
- mixing public alpha tutorial polish into engine tooling work.

Acceptance criteria:

- build/run commands clearly explain artifact, source-input, V8, and unsupported paths;
- artifact inspection is deterministic and Plan-driven;
- doctor/audit report real metadata and explicit optional-gate status;
- OpenAPI route skeleton policy is honest about schema/security gaps;
- dev/watch is either deliberately deferred or implemented with cache/stale cleanup rules;
- command diagnostics use the shared stable format.

Non-goals:

- package manager;
- project scaffolder;
- cloud deployment;
- public alpha launch docs.

Suggested PR grouping:

- build/run UX and artifact inspection;
- doctor/audit real checks and OpenAPI skeleton policy;
- source-input/dev-watch decision follow-through.

Parallelization notes: audit/doctor fixture work can proceed with Plan-driven data while
source-input run remains blocked. Watch should not start until source-input semantics are
settled.

## ENGINE-19: Conformance Harness and Runtime Compatibility Suite

Goal: make engine behavior executable and protected end-to-end.

Why it exists: the conformance suite already proves selected compiler and V8-gated paths.
The foundation needs a broader compatibility matrix that ties compiler, Plan, runtime,
V8, HTTP, async, SQLite, capability denial, lifecycle cleanup, package smoke, and optional
evidence lanes together without confusing default and optional gates.

Dependencies:

- ENGINE-13 HTTP behavior;
- ENGINE-14 bootstrap/module loading;
- ENGINE-15 diagnostic goldens;
- ENGINE-16 lifecycle cleanup hooks;
- ENGINE-17 SQLite capability-wired runtime;
- ENGINE-18 CLI/evidence reporting.

Task breakdown:

- TASK ENGINE-19.A: Foundation Conformance Matrix;
- TASK ENGINE-19.B: V8-Gated Runtime Conformance;
- TASK ENGINE-19.C: HTTP and Async Conformance;
- TASK ENGINE-19.D: SQLite and Capability Conformance;
- TASK ENGINE-19.E: Package Outside-Checkout Smoke.

Implementation risks:

- default non-V8 gates being reported as V8 success;
- skipped optional services or SDKs being treated as pass claims;
- conformance fixtures drifting from public examples;
- package smoke being confused with release readiness;
- using benchmark-looking smoke as performance evidence.

Acceptance criteria:

- conformance matrix lists default, V8-gated, package, live-provider, stress, and
  benchmark lanes separately;
- compiler-to-Plan-to-runtime-to-V8-to-HTTP paths are covered where implemented;
- async, body/header, SQLite, capability denial, and lifecycle cleanup cases are covered
  when the corresponding implementation exists;
- package outside-checkout smoke proves local artifact layout only;
- failure and skip output is deterministic and honest.

Non-goals:

- benchmark marketing;
- exhaustive fuzzing unless separately scoped;
- live PostgreSQL/SQL Server.

Suggested PR grouping:

- foundation matrix and naming/layout;
- V8/HTTP/async/SQLite conformance expansion;
- package outside-checkout smoke and evidence reporting.

Parallelization notes: unsupported negative fixtures can be prepared while positive runtime
features land. Package smoke should stay separate if archive churn becomes noisy.

## ENGINE-20: Strong Plan Strategic Layer

Goal: turn `app.plan.json` into Slop's strategic advantage, not just metadata.

Why it exists: Sloppy's Plan is the native runtime's chance to know the app before running
JavaScript. A strong Plan can drive startup validation, capability enforcement, artifact
hashing, doctor/audit, future OpenAPI, future optimization, compatibility policy, and
conformance without dynamic runtime guessing.

Dependencies:

- ENGINE-02 compiler metadata emission;
- ENGINE-13 route/request/backend needs;
- ENGINE-14 artifact/module graph needs;
- ENGINE-17 provider/capability graph needs;
- ENGINE-18 doctor/audit/inspection commands;
- ENGINE-19 conformance matrix.

Task breakdown:

- TASK ENGINE-20.A: Typed Plan Graph Model;
- TASK ENGINE-20.B: Static Validation and Compatibility Strategy;
- TASK ENGINE-20.C: Plan-Driven Audit and Doctor Strategy;
- TASK ENGINE-20.D: Plan-Driven OpenAPI/Optimization Future Hooks;
- TASK ENGINE-20.E: Plan Versioning and Evolution Policy.

Implementation risks:

- overfitting the Plan before public API stabilizes;
- allowing runtime to infer app shape from JS instead of validated artifacts;
- adding OpenAPI/optimization metadata that implies implemented features;
- breaking compatibility without a versioning/evolution policy;
- making doctor/audit depend on ad hoc command parsing rather than Plan data.

Acceptance criteria:

- typed route, handler, capability, provider, and artifact graph models are documented;
- native static validation rejects unsupported/malformed plans before app execution;
- compatibility/version policy is explicit and tested;
- doctor/audit consume Plan data rather than running handlers;
- future OpenAPI, optimization, policy, and tooling hooks are reserved without claiming
  implementation.

Non-goals:

- overfitting Plan before public API stabilizes;
- full OpenAPI schema generator now;
- visual designer/tooling now.

Suggested PR grouping:

- typed graph model and validation;
- audit/doctor strategy and compatibility policy;
- future hooks and version-evolution docs/tests.

Parallelization notes: graph docs can be written before all producers exist, but native
validation should only enforce fields that the compiler/runtime can honestly produce and
consume.

## Recommended Order

1. ENGINE-20 frames the stronger Plan graph enough for HTTP, modules, SQLite, CLI, and
   conformance to share vocabulary.
2. ENGINE-14 and ENGINE-15 can proceed in parallel where source names, bootstrap loading,
   and diagnostics intersect.
3. ENGINE-16 should land before broad resource-owning SQLite and shutdown-heavy HTTP work.
4. ENGINE-13 uses ENGINE-12/16 primitives to turn HTTP into a proper backend.
5. ENGINE-17 completes SQLite once resource ownership and capability policy are wired.
6. ENGINE-18 tightens the command loop after the runtime and Plan evidence surfaces exist.
7. ENGINE-19 wraps the implemented behavior in executable compatibility evidence.

This order is strategic, not a ban on parallel docs, fixtures, or narrow preparatory work.
Any PR that crosses runtime behavior must still stay bounded to one coherent ownership
surface.
