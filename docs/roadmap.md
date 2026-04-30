# Roadmap

## Purpose

This roadmap is the project-state source document after the initial EPIC-00 through
EPIC-20 roadmap batch. It is intentionally an audit, not a release promise.

GitHub issues mirror these docs. An issue existing or being closed is not proof that a
feature works; status below is based on checked-in source, docs, tests, and current
tooling.

## 2026-04-29 Strategic Update

MAIN and MAIN.1 have now landed through PRs #240-#255 for their scoped verification and
hardening work. The older EPIC audit below remains useful history, but detailed "deferred"
phrasing in those rows may be older than the post-MAIN1 codebase. For the current strategic
source of truth, use:

- `docs/project/strategic-current-state-audit.md`;
- `docs/project/strategic-system-audit.md`;
- `docs/project/slop-engine-final-shape.md`;
- `docs/project/engine-framework-contract.md`;
- `docs/project/slop-engine-layered-roadmap.md`.

The next phase is Slop Engine foundation completion: real supported compiler pipeline, real
V8 runtime integration, async handler/Promise policy, framework HTTP API runtime,
SQLite end-to-end with capability enforcement, cancellation/deadline/backpressure
infrastructure, app/request lifecycle, diagnostics/source maps, conformance examples, and
packaged evidence.

ENGINE-03 now covers the bounded V8 owner-thread microtask async boundary. Sloppy still
aims at a full scalable async runtime, but that is not implied by ENGINE-03. ENGINE-12
(#306 through #310) tracks the future layer for native completion queues, owner-thread V8
continuation scheduling, deadline/shutdown drain policy, bounded async backpressure,
provider/offload policy hooks, and stress evidence. Implement ENGINE-12 when a real
external async source must cross the runtime boundary, and before any public alpha,
benchmark, or product claim says Sloppy has scalable async performance or production-ready
async lifecycle behavior.

ENGINE-23 now splits provider execution out of ENGINE-12 and before ENGINE-13/17 work that
would otherwise depend on provider/offload behavior. It owns provider operation
descriptors, per-provider-instance executors, serialized blocking execution for
SQLite-class providers, bounded blocking pools, nonblocking provider mode,
capability-gated admission, cancellation/timeout/shutdown/late-completion semantics,
worker lifecycle, diagnostics, and stress evidence without benchmark claims.

ENGINE-13 through ENGINE-20 now track the remaining full engine foundation after
ENGINE-12 and ENGINE-23: proper HTTP runtime backend, module/bootstrap completion, source
maps and diagnostics, app/resource lifetime, SQLite data runtime completion, CLI/dev loop,
conformance compatibility, and the strong Plan strategic layer. ENGINE-21 and ENGINE-22
add the cross-cutting memory/string foundation: primitive lifetime/allocation/string/
builder contracts and bounded app/static string interning first, then adoption in HTTP,
V8, SQLite, diagnostics, Plan/artifact loading, CLI, and hot-path conformance. Proper
async, provider execution, and proper HTTP are intentionally separate: ENGINE-12 owns
native async completion and owner-thread continuation mechanics, ENGINE-23 owns
provider/offload execution, and ENGINE-13 owns HTTP listener, connection, parser, body,
keep-alive, timeout, cancellation, backpressure, graceful shutdown, and server diagnostic
policy.

Direct source-input `sloppy run app.js` remains unsupported today, but is now tracked by
#302 as a compiler/CLI handoff task once the compiler can emit complete artifacts for
realistic supported apps. The current supported workflow remains explicit `sloppyc build`
plus `sloppy run --artifacts`.

Public alpha docs remain blocked. PostgreSQL and SQL Server JS bridges, benchmark claims,
ORM/migrations, package-manager behavior, Node compatibility, production-grade HTTP server
breadth, and memory/string hot-path adoption claims are deferred until the engine
foundation is coherent. SQLite runtime completion depends on ENGINE-23 before scalable
provider execution can be claimed.

## Status Key

- Done: the scoped foundation slice is implemented and covered by default checks.
- Mostly done: the scoped slice exists, but important validation is gated, follow-up work
  remains, or the GitHub/project state still needs cleanup.
- Partial: a useful subset exists, but advertised scope still has open work.
- Deferred: intentionally not implemented in this roadmap batch.
- Blocked: cannot be validated or completed without a missing dependency or decision.
- Superseded: replaced by a later plan or different scope.

## EPIC Audit

| EPIC | Status | Current reality |
| --- | --- | --- |
| EPIC-00 Foundation / Harness / Tooling | Mostly done | AGENTS, standards, project docs, GitHub ceremony tooling, review helpers, and source-only archive tooling exist. Several original GitHub task issues remain open and should be reconciled rather than treated as current blockers. |
| EPIC-01 Platform Abstraction | Partial | Platform directories, boundary docs, scanner checks, and a small platform time abstraction exist. Scanner fixtures/self-tests, broader OS API taxonomy, and CI/platform expansion remain open. |
| EPIC-02 Core Native Foundation | Done | Core status, source locations, strings, bytes, checked math, and assertion behavior exist with CTest coverage. |
| EPIC-03 Memory Foundation | Partial | `SlArena` exists with tests and docs. String builder/buffer foundation and fuller allocator policy remain open and are superseded by ENGINE-21/22 for the strategic engine foundation. |
| EPIC-04 Diagnostics Foundation | Partial | Diagnostic builder, stable codes, text rendering, redaction placeholder, and snapshot fixtures exist. Source frames, JSON diagnostics, source maps, and richer metadata remain open. |
| EPIC-05 Resource Lifecycle Foundation | Partial | `SlScope` cleanup/lifetime skeleton exists with tests. Generation-counted resource table and leak reporting remain open. |
| EPIC-06 Plan Schema and Loader | Mostly done | Minimal Plan v1 structs, JSON parsing, validation, arena-owned parsed strings, handler lookup, and golden fixture matrix exist. File-based loading, compatibility checks, hashes, routes/modules/providers, and source-map diagnostics remain deferred. |
| EPIC-07 V8 Bridge Smoke | Mostly done | V8 SDK detection, isolated C ABI, noop engine, V8 classic-script smoke, function calls, and exception mapping exist behind an explicit V8-enabled build. Default gates do not validate V8. SDK distribution remains deferred. |
| EPIC-08 Handwritten Artifact Execution | Mostly done | A V8-gated handwritten `app.plan.json` plus `app.js` smoke path invokes a numeric handler ID through the runtime-contract boundary. Compiler output, ESM module loading, HTTP contexts, and full result conversion remain deferred. |
| EPIC-09 Event Loop / Concurrency Foundation | Mostly done | `SlLoop`, `SlAsync`, and inline `SlWorkerPool` skeletons exist with deterministic C tests. They are single-threaded/caller-backed skeletons, not libuv integration, real threads, V8 promises, cancellation-token propagation, deadlines, bounded queues, or backpressure. |
| EPIC-10 HTTP Router Foundation | Mostly done | Route pattern parser/matcher, complete-buffer HTTP/1 request-head parser through llhttp, libuv init smoke, synthetic in-memory GET dispatch to handler ID, and EPIC-23 dev-only response/context wiring exist. EPIC-22 adds a dev-only socket loop in the CLI, but production-grade/server-ready streaming parser state, request body parsing, middleware, and production route table remain deferred. |
| EPIC-11 Public TypeScript API Bootstrap | Mostly done | Bootstrap stdlib layout, `Results.text/json`, `Sloppy.create`, in-memory `app.mapGet`, names, static checks, and `examples/hello` exist. Bare `"sloppy"` import, compiler extraction, plan emission, runtime ESM loading, and `app.run` remain deferred. |
| EPIC-12 App Host Foundation | Mostly done | `Sloppy.createBuilder`, `builder.build`, structural `app.freeze`, object config, memory logging, and string-token services exist in the bootstrap stdlib. Native app-host validation and request-scoped lifetimes remain deferred. |
| EPIC-13 Developer Ergonomics Layer | Mostly done | Route groups, expanded `Results.*`, schema skeleton, metadata storage, and ergonomics examples exist as JavaScript-only bootstrap behavior. EPIC-23 adds narrow native response/context support for compiler artifacts; automatic validation, OpenAPI schema generation, broader request binding, and full app-host integration remain deferred. |
| EPIC-14 Modularity / App Modules | Mostly done | `Sloppy.module`, `builder.addModule`, dependency ordering, phase execution, module attribution, debug metadata, and module examples exist in the bootstrap stdlib. Compiler extraction, package loading, real plan module emission, and native plugin boundaries remain deferred. |
| EPIC-15 Data / Capabilities Foundation | Mostly done | Database capability metadata, query template lowering, fake data provider contract, transaction callback semantics, and data examples exist. Capability enforcement, permissions, app-plan data provider entries, and non-SQLite JS-to-native provider resources remain deferred. |
| EPIC-16 SQLite Provider | Mostly done | Native C SQLite provider supports in-memory/file open, exec/query/queryOne, primitive parameter binding, transactions, diagnostics, and tests. MAIN1-08 adds a V8-gated JavaScript SQLite bridge through resource IDs; default non-V8 paths and public source examples remain honest about their limits. |
| EPIC-17 PostgreSQL Provider | Mostly done | Native libpq provider supports connection strings, parameterized `$1` queries, exec/query/queryOne, transactions, redaction diagnostics, tiny pool skeleton, default non-live tests, and env-gated live tests. JS bridge, production pooling, cancellation, and packaging hardening remain deferred. |
| EPIC-18 SQL Server Provider | Mostly done | Native ODBC provider supports connection strings, `?` parameters, exec/query/queryOne, transactions, missing-driver diagnostics, redaction, tiny pool skeleton, default non-live tests, and env-gated live tests. JS bridge, production pooling, cancellation, and cross-platform SQL Server policy remain deferred. |
| EPIC-19 CLI Introspection Tooling | Mostly done | `sloppy routes`, `sloppy doctor`, `sloppy audit`, and `sloppy openapi` inspect plan-compatible metadata with golden tests and no handler/server/V8/live-DB execution. Real compiler/app-host metadata emission and full audit/OpenAPI behavior remain deferred. |
| EPIC-20 Benchmarks / Performance Validation | Mostly done | `sloppy_bench`, Windows wrapper, list/smoke checks, route matcher, HTTP request-head parser, handler lookup/noop dispatch, and synthetic dispatch benchmarks exist with methodology docs. Real HTTP throughput, V8 handler timing, JSON serialization, live DB benchmarks, trend tracking, and external comparisons remain deferred. |

## Current State By Area

### Foundation / Harness

Exists now: repo docs, ADRs, AGENTS guide, contribution workflow, project issue metadata,
review playbooks, execution-plan folders, Windows wrappers, CMake/Cargo skeletons,
GitHub issue creation tooling, source-only review zip tooling, docs freshness structure
checks, platform/C standards scanners, and quality score/debt trackers.

Works now: Windows-first bootstrap/configure/build/test/format/lint workflow, issue data
validation/dry-run summaries, and source-only review archives.

Not implemented or incomplete: several legacy GitHub task issues are still open despite
repo-side work existing; docs link checking is not implemented; semantic docs freshness is
still mostly human-reviewed.

### Platform Boundaries

Exists now: `src/platform/*` directories, `include/sloppy/platform.h`, platform time
abstraction, and scanners that reject common OS headers outside platform locations.

Works now: default lint checks platform and C standards boundaries.

Deferred: scanner fixtures/self-tests, richer platform API categories, Linux/macOS CI, and
future platform-specific test suites.

### Core Primitives

Exists now: status, source locations, strings, bytes, checked math, assertions, and C/C++
syntax probes.

Works now: default CTest coverage validates documented primitive behavior.

Deferred: final buffer/string builder API and broader allocator-backed helpers.

### Arena / Memory

Exists now: caller-backed `SlArena`, marks, reset, generation validation, high-water
tracking, alignment checks, and release-visible tests.

Works now: arena behavior is covered in default tests.

Deferred: allocator interface, string builder/buffer foundation, bounded string
interning/symbol tables, owned string/buffer policy, stricter raw allocation enforcement,
sanitizer/fuzz matrix, hot-path adoption, and request/app lifetime integration. ENGINE-21
owns the primitive memory/string foundation and ENGINE-22 owns adoption/refactor across
HTTP, V8, SQLite, diagnostics, Plan/artifacts, CLI, and conformance.

### Diagnostics

Exists now: diagnostic data model, builder, severity/code mapping, text renderer, snapshot
fixtures, and provider/runtime diagnostic codes.

Works now: deterministic diagnostics for implemented parsers/providers/CLI paths.

Deferred: JSON diagnostic output, source frames, source maps, structured fixes, localization,
and stronger redaction policy.

### Resource Lifecycle

Exists now: `SlScope` cleanup skeleton.

Works now: scope cleanup ordering and lifecycle behavior are covered by default C tests.

Not implemented: leak reports and resource cleanup callbacks through request/app scopes.
The generation-counted resource table exists, and MAIN1-08 adds the first V8-gated
SQLite-only JS-visible resource IDs; general provider resource IDs remain deferred.

### Plan Loader

Exists now: minimal Plan v1 native structs, yyjson parser/validator, handler lookup, and
golden fixtures.

Works now: in-memory JSON bytes parse into arena-owned plan data; malformed plan fixtures
produce diagnostics.

Deferred: file loading, compatibility/version checks, hash verification, route/module/data
provider sections, source maps, and full startup validation.

### V8 Bridge

Exists now: optional SDK detection, manifest validation, isolated C ABI, noop engine,
classic-script V8 smoke, function call smoke, and exception diagnostics.

Works now: only when `SLOPPY_ENABLE_V8=ON` and a valid SDK are configured. Default gates
intentionally do not prove V8.

Deferred: SDK hosting/release packaging, ESM loading, bootstrap runtime loading, intrinsics,
handler registration, source maps, promises, owner-thread checks, and shutdown policy.

### Handwritten Execution

Exists now: V8-gated handwritten plan/bundle fixture and runtime-contract helper that calls
a numeric handler ID.

Works now: simple copied string result through the V8 smoke path.

Deferred: compiler artifacts, module loading, request contexts, response descriptors, full
result conversion, route params, and HTTP response writing.

### Event Loop / Concurrency Skeleton

Exists now: caller-backed `SlLoop`, `SlAsync`, and inline `SlWorkerPool` skeletons.

Works now: deterministic single-threaded completion and settlement tests.

Deferred but foundation-required: libuv backend, real worker threads, cross-thread posting,
OS wakeups, V8 microtasks/promises, request-scope retention, cancellation-token
propagation, deadline hooks, bounded queues, and backpressure diagnostics.

### HTTP / Router Foundation

Exists now: route pattern parser/matcher, HTTP request-head parser, libuv link smoke, and
synthetic GET dispatch to a numeric handler ID.

Works now: complete-buffer parser and synthetic in-memory dispatch tests.

Deferred: production-grade TCP server / production socket loop, streaming llhttp state,
bodies, response writer, request context, route params in JS context, route table/trie or
equivalent, precedence, query parsing, percent decoding, middleware, and public API
integration. A dev-only socket loop exists under EPIC-22.

### Public TypeScript API Bootstrap

Exists now: source-controlled ESM stdlib with `Sloppy`, `Results`, `schema`, `data`,
builder/app/module services, and examples.

Works now: static checks and optional Node-based ESM checks validate bootstrap API shape.
These checks are not a Node compatibility claim.

Deferred: bare import support, compiler extraction, app-plan emission, V8-backed ESM
stdlib tests, `app.run`, `app.listen`, HTTP serving, package-manager behavior, and Node
compatibility.

### App Host / Ergonomics / Modules

Exists now: JavaScript-only builder/app freeze, config, memory logging, string-token
services, route groups, result helpers, schema skeleton, modules, module phases, dependency
ordering, and debug metadata.

Works now: bootstrap tests/examples prove in-memory behavior.

Deferred: native app graph, startup validation, request-scoped services, disposal hooks,
module packages, native plugins, automatic validation responses, middleware/filters, and
real plan emission.

### Data / Capabilities

Exists now: bootstrap data/capability metadata, query template lowering, fake providers,
transactions, native SQLite/PostgreSQL/SQL Server provider boundaries, and redaction.

Works now: native provider C tests cover default non-live behavior; PostgreSQL and SQL
Server live execution is opt-in through environment variables. JavaScript examples still
use fake providers or bridge-unavailable paths.

Deferred: capability enforcement, filesystem/network capabilities, general JS-visible
native resource IDs beyond the V8-gated SQLite bridge, app-plan provider/capability
sections, production pooling, async offload, cancellation, migrations, file DB policy,
TLS/options hardening, and provider package distribution.

### CLI Introspection

Exists now: metadata-only routes, doctor, audit, and OpenAPI commands.

Works now: deterministic plan-compatible fixtures and golden outputs.

Deferred: compiler/app-host metadata emission, live provider checks behind explicit flags,
full OpenAPI schema generation, richer audit rules, source mapping, and output polish.

### Benchmarks

Exists now: benchmark executable, Windows wrapper, route/parser/handler/synthetic dispatch
benchmarks, JSON/text output, and sample output shape.

Works now: list/smoke checks verify harness behavior. Release local runs can measure the
implemented foundations.

Deferred: real HTTP throughput, JSON serialization, live DB benchmarks, V8 handler timing,
external runtime comparisons, dashboards, uploads, and CI performance gates.

## GitHub Issue Reality

The 2026-04-29 live audit initially found no open PRs, merged MAIN/MAIN1 PRs #240-#255,
completed-but-open parent issues #167/#168/#180-#192, blocked public-alpha issues
#194/#237-#239, deferred benchmark issues #193/#234-#236, and old EPIC-00..EPIC-05
leftovers needing focused review.

After review in PR #256, completed/superseded stale issues were closed with evidence or
replacement-roadmap comments. The remaining pre-ENGINE open issues are #26 and #32 because
they still represent focused unfinished platform-scanner and string/buffer work. See
`docs/project/strategic-current-state-audit.md` and
`docs/project/strategic-issue-cleanup-plan.md` for the cleanup record.

## Next Roadmap

The EPIC-21 through EPIC-26 implementation batch has now landed for its scoped MVPs:
compiler extraction, dev-only artifact run, HTTP response/request context, classic
bootstrap runtime handoff, experimental local packaging, and default non-V8 hosted CI.

Active planning has moved to:

1. [Slop Engine Final Shape](project/slop-engine-final-shape.md): define the intended
   engine/framework foundation before higher-level framework perks.
2. [Engine Framework Contract](project/engine-framework-contract.md): lock the ENGINE-01
   JS/API, async, HTTP, SQLite, cancellation, limit, and deferred-behavior decisions.
3. [Slop Engine Layered Roadmap](project/slop-engine-layered-roadmap.md): Layer 0 cleanup
   through Layer 10 public-alpha gate.
4. [ROADMAP MAIN](project/roadmap-main.md) and
   [ROADMAP MAIN.1](project/roadmap-main-1-hardening.md): historical input from the narrow
   alpha-path planning and hardening pass.

Capability enforcement is now an engine-foundation blocker specifically because public
alpha docs/examples need honest evidence for the capability-enforced SQLite JS/native
bridge, and for the providers that remain deferred. EPIC-28-style public alpha
docs/examples are deferred until the engine foundation layers pass or are explicitly scoped
down with honest exclusions.

See also `docs/project/current-issue-state-audit.md` and
`docs/project/strategic-issue-cleanup-plan.md` for the stale issue cleanup plan.
