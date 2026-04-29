# Roadmap

## Purpose

This roadmap is the project-state source document after the initial EPIC-00 through
EPIC-20 roadmap batch. It is intentionally an audit, not a release promise.

GitHub issues mirror these docs. An issue existing or being closed is not proof that a
feature works; status below is based on checked-in source, docs, tests, and current
tooling.

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
| EPIC-03 Memory Foundation | Partial | `SlArena` exists with tests and docs. String builder/buffer foundation and fuller allocator policy remain open. |
| EPIC-04 Diagnostics Foundation | Partial | Diagnostic builder, stable codes, text rendering, redaction placeholder, and snapshot fixtures exist. Source frames, JSON diagnostics, source maps, and richer metadata remain open. |
| EPIC-05 Resource Lifecycle Foundation | Partial | `SlScope` cleanup/lifetime skeleton exists with tests. Generation-counted resource table and leak reporting remain open. |
| EPIC-06 Plan Schema and Loader | Mostly done | Minimal Plan v1 structs, JSON parsing, validation, arena-owned parsed strings, handler lookup, and golden fixture matrix exist. File-based loading, compatibility checks, hashes, routes/modules/providers, and source-map diagnostics remain deferred. |
| EPIC-07 V8 Bridge Smoke | Mostly done | V8 SDK detection, isolated C ABI, noop engine, V8 classic-script smoke, function calls, and exception mapping exist behind an explicit V8-enabled build. Default gates do not validate V8. SDK distribution remains deferred. |
| EPIC-08 Handwritten Artifact Execution | Mostly done | A V8-gated handwritten `app.plan.json` plus `app.js` smoke path invokes a numeric handler ID through the runtime-contract boundary. Compiler output, ESM module loading, HTTP contexts, and full result conversion remain deferred. |
| EPIC-09 Event Loop / Concurrency Foundation | Mostly done | `SlLoop`, `SlAsync`, and inline `SlWorkerPool` skeletons exist with deterministic C tests. They are single-threaded/caller-backed skeletons, not libuv integration, real threads, V8 promises, cancellation, deadlines, or backpressure. |
| EPIC-10 HTTP Router Foundation | Mostly done | Route pattern parser/matcher, complete-buffer HTTP/1 request-head parser through llhttp, libuv init smoke, synthetic in-memory GET dispatch to handler ID, and EPIC-23 dev-only response/context wiring exist. EPIC-22 adds a dev-only socket loop in the CLI, but production-grade/server-ready streaming parser state, request body parsing, middleware, and production route table remain deferred. |
| EPIC-11 Public TypeScript API Bootstrap | Mostly done | Bootstrap stdlib layout, `Results.text/json`, `Sloppy.create`, in-memory `app.mapGet`, names, static checks, and `examples/hello` exist. Bare `"sloppy"` import, compiler extraction, plan emission, runtime ESM loading, and `app.run` remain deferred. |
| EPIC-12 App Host Foundation | Mostly done | `Sloppy.createBuilder`, `builder.build`, structural `app.freeze`, object config, memory logging, and string-token services exist in the bootstrap stdlib. Native app-host validation and request-scoped lifetimes remain deferred. |
| EPIC-13 Developer Ergonomics Layer | Mostly done | Route groups, expanded `Results.*`, schema skeleton, metadata storage, and ergonomics examples exist as JavaScript-only bootstrap behavior. EPIC-23 adds narrow native response/context support for compiler artifacts; automatic validation, OpenAPI schema generation, broader request binding, and full app-host integration remain deferred. |
| EPIC-14 Modularity / App Modules | Mostly done | `Sloppy.module`, `builder.addModule`, dependency ordering, phase execution, module attribution, debug metadata, and module examples exist in the bootstrap stdlib. Compiler extraction, package loading, real plan module emission, and native plugin boundaries remain deferred. |
| EPIC-15 Data / Capabilities Foundation | Mostly done | Database capability metadata, query template lowering, fake data provider contract, transaction callback semantics, and data examples exist. Capability enforcement, permissions, app-plan data provider entries, and JS-to-native provider resources remain deferred. |
| EPIC-16 SQLite Provider | Mostly done | Native C SQLite provider supports in-memory/file open, exec/query/queryOne, primitive parameter binding, transactions, diagnostics, and tests. JavaScript `data.sqlite.open` still fails honestly because runtime intrinsics/resource IDs are not wired. |
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

Deferred: allocator interface, string builder/buffer foundation, stricter raw allocation
enforcement, sanitizer matrix, and request/app lifetime integration.

### Diagnostics

Exists now: diagnostic data model, builder, severity/code mapping, text renderer, snapshot
fixtures, and provider/runtime diagnostic codes.

Works now: deterministic diagnostics for implemented parsers/providers/CLI paths.

Deferred: JSON diagnostic output, source frames, source maps, structured fixes, localization,
and stronger redaction policy.

### Resource Lifecycle

Exists now: `SlScope` cleanup skeleton.

Works now: scope cleanup ordering and lifecycle behavior are covered by default C tests.

Not implemented: generation-counted resource table, JS-visible resource IDs, leak reports,
kind validation, and resource cleanup callbacks through request/app scopes.

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

Deferred: libuv backend, real worker threads, cross-thread posting, OS wakeups, V8
microtasks/promises, request-scope retention, cancellation, deadlines, and backpressure.

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

Deferred: capability enforcement, filesystem/network capabilities, JS-visible native
resource IDs, app-plan provider/capability sections, production pooling, async offload,
cancellation, migrations, file DB policy, TLS/options hardening, and provider package
distribution.

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

The GitHub issue set no longer mirrors repo reality cleanly:

- many child task issues for EPIC-06 through EPIC-20 are closed;
- the parent EPIC issues remain open with `status:deferred` labels;
- older EPIC-00/01/03/04/05 task issues remain open even where repo-side work partly or
  mostly exists;
- milestones remain open with old open-issue counts.

See `docs/project/post-0.7-issue-audit.md` for recommended cleanup. This roadmap PR does
not mutate GitHub issues.

## Next Roadmap

The next coherent roadmap batch originally started at EPIC-21. EPIC-21 Compiler Extraction
MVP and EPIC-22 Sloppy Run MVP are complete in the repository, so the remaining tracked
batch in `docs/project/next-roadmap.md` starts at EPIC-23. The recommended remaining order
is:

1. EPIC-23 HTTP Response Writer and Request Context.
2. EPIC-24 V8 Module Loading and Bootstrap Runtime.
3. EPIC-25 Release Packaging and Distribution. Done for experimental local packages.
4. EPIC-26 Cross-platform CI Expansion. Done for required non-V8 hosted CI; optional V8
   SDK setup, live provider services, and package smoke remain follow-ups.
5. EPIC-27 Runtime Security / Capabilities Enforcement.
6. EPIC-28 Public Alpha Docs and Examples.

The remaining batch deliberately begins with HTTP response/request-context integration
instead of adding more isolated provider or ergonomics surface.
