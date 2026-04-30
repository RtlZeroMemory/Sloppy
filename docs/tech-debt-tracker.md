# Technical Debt Tracker

This tracker records real deferred work after the EPIC-00 through EPIC-20 roadmap batch.
It is not a wishlist for random expansion; items here are known gaps between current repo
state and an honest public-alpha path.

## Strategic Engine Foundation Blockers

These blockers now define the next Slop Engine phase. They supersede treating benchmark
methodology, public alpha docs, PostgreSQL JS bridge work, or SQL Server JS bridge work as
immediate product blockers:

ENGINE-01 locks the framework contract in
`docs/project/engine-framework-contract.md`; future implementation debt should be tracked
against that contract rather than reopening ambiguous "minimum alpha" scope.

- Compiler/runtime completion for realistic supported Sloppy apps beyond ENGINE-02 and
  ENGINE-03: source-input handoff/cache, module/service/schema extraction, broader async
  source shapes, non-GET dispatch, and provider/capability enforcement.
- Full scalable async runtime beyond ENGINE-03: ENGINE-12 (#306, tasks #307-#310) owns
  native async completion queues/backends, owner-thread V8 continuation scheduling for
  native completions, timer/fetch policy if ever scoped, HTTP disconnect/shutdown drain
  behavior, richer deadline hooks, bounded queue/backpressure diagnostics, provider/offload
  integration, stress evidence, source-remapped async diagnostics, and provider-backed
  cancellation. ENGINE-03 covers returned Promises that settle during the owner-thread
  microtask drain, rejection diagnostics, pending-Promise failure, cancellation snapshots,
  and request-scope cleanup for the bounded call.
- Proper HTTP runtime backend: ENGINE-13 owns listener/backend architecture, connection and
  request lifecycle, parser limits, header/body buffering policy, keep-alive, deadlines,
  cancellation, backpressure, graceful shutdown, server diagnostics, and stress/conformance
  smoke. This is separate from ENGINE-12 because HTTP has parser, connection, body, and
  shutdown policy that sits above generic async completions.
- Module/bootstrap completion: ENGINE-14 owns stdlib/bootstrap asset loading, app module
  loading, ESM/classic decision, module cache, import rewrite and intrinsic boundaries,
  source names, reload/dev-loop implications, and V8 startup diagnostics.
- Source maps and diagnostics completion: ENGINE-15 owns compiler maps, generated
  artifact mapping, V8 exception remapping, async diagnostic JSON/source frames, redaction,
  stable codes, CLI diagnostic format, and diagnostic goldens.
- App/resource lifetime runtime: ENGINE-16 owns app startup/shutdown, request/app scopes,
  resource cleanup, cancellation propagation, leak-oriented hooks, and lifecycle
  diagnostics.
- SQLite runtime completion: ENGINE-17 owns the public JS SQLite API, native bridge,
  capability-wired open/use, query/exec/queryOne, transactions/prepared-statement
  decision, result mapping, file and in-memory policy, cleanup, cancellation/deadline
  behavior, and users API proof.
- CLI/dev loop runtime: ENGINE-18 owns `sloppyc`/`sloppy run` UX, source-input run
  decision, artifact inspection, doctor, audit, OpenAPI route skeleton policy, watch/dev
  decision, and command diagnostics.
- Conformance compatibility suite: ENGINE-19 owns compiler to Plan to runtime to V8 to
  HTTP evidence, async/body/header/SQLite/capability/lifecycle/package cases, and default
  versus optional gate reporting.
- Strong Plan strategic layer: ENGINE-20 owns typed route/handler/capability/provider/
  artifact graphs, static validation, compatibility, doctor/audit, future OpenAPI and
  optimization hooks, versioning, and internal tooling leverage.
- Framework HTTP API runtime: method dispatch, headers in context, JSON/text body policy,
  header/body limits, request cancellation signal, timeout hooks, backpressure behavior,
  result serialization, and error response contract.
- SQLite end-to-end: public JS handler path through native provider, capability enforcement,
  cancellation-aware operation boundaries, app/request ownership, transactions/prepared
  statement decision, and executable users API conformance.
- Capability/security integration: bridge enforcement before provider work and no OS
  sandbox claims.
- App-host lifecycle/resource completion beyond ENGINE-07: provider ownership policy,
  graceful drain/force-cancel shutdown behavior for real native async work, bounded
  resource budgets, leak checks where possible, and richer cleanup-failure diagnostics.
- Conformance/examples/packaging evidence: realistic examples and packaged runtime smoke
  outside the checkout before public alpha docs.

## Must Fix Before Public Alpha

- Public API to plan emission beyond ENGINE-02: bootstrap module/service/schema metadata
  must become compiler-produced plan metadata instead of in-memory debug snapshots.
- `sloppy run` source/build handoff: EPIC-22 can load artifacts, but source input still
  needs a clean `sloppyc` handoff/cache story.
- Source-input `sloppy run <source.js>` implementation: MAIN1-01 decided that alpha keeps
  the explicit two-step artifact workflow. #302 now tracks the dedicated source-input
  handoff task after the compiler emits full supported-app artifacts. Implementing direct
  source input still needs a scoped compiler handoff, cache keys, stale-artifact checks,
  source diagnostics, cleanup policy, and rebuild policy.
- HTTP production response pipeline beyond EPIC-23: custom headers, redirect helpers,
  streaming/files, cookies, content negotiation, keep-alive policy, and production error
  pages.
- Request context model beyond EPIC-23: body/header access, typed/coerced route/query
  binding, services/config/log injection, and real request-scoped lifetime boundaries.
- V8 module loading beyond EPIC-24: true ESM loading, production module cache, richer source
  maps, native async completion queues, HTTP disconnect/shutdown cancellation, and
  executable public examples through the final bootstrap module shape remain open.
  ENGINE-03 adds microtask-only Promise settlement for direct async handlers; it does not
  add full JavaScript event-loop behavior. ENGINE-12 should be implemented when real
  external async sources are ready to cross the runtime boundary, and before Sloppy makes
  scalable async, async provider, async HTTP lifecycle, or performance claims. The
  compiler rejection `SLOPPYC_E_UNSUPPORTED_ASYNC_HANDLER_BODY` remains valid for `await`,
  multi-statement async bodies, and non-direct async returns until those shapes become
  executable.
- MAIN1-12 package/CI hardening follow-ups: exact dynamic V8 runtime file lists, hosted
  prebuilt SDK source, V8-enabled package execution validation, hosted package CI evidence,
  and stable sanitizer/fuzz jobs remain open.
- SQLite JS bridge capability integration: MAIN1-10 adds the runtime registry and provider
  policy hook, but the JavaScript-to-native SQLite bridge must call it once MAIN1-08 exposes
  the stable bridge boundary. MAIN1-13 records this as a deferred capability conformance
  marker rather than claiming JavaScript bridge enforcement.
- Live DB service infrastructure for PostgreSQL and SQL Server: opt-in local env vars and
  separate skipped CTest gates make reporting honest, but hosted service jobs are still
  needed for release confidence.
- Cross-platform CI hardening: default Linux clang/gcc, macOS clang, Windows clang-cl, and
  explicit V8/provider-gated reporting exist; local V8 SDK discovery is centralized through
  the Windows resolver, but hosted prebuilt SDK/cache setup, optional live provider service
  jobs, sanitizer/fuzz jobs, and package smoke remain open.
- Cross-platform release validation: local Windows ZIP tooling, Unix TAR tooling,
  checksums, and outside-checkout package smoke exist, but hosted Linux/macOS package
  execution, V8 package execution smoke, release hardening, and CI validation remain open
  after MAIN1-12.
- Public alpha docs/examples: at least one executable hello must run through the real
  Sloppy toolchain. MAIN1-08 adds a real V8-gated SQLite JS-native bridge fixture, but the
  public source-stdlib SQLite tutorial remains deferred until the compiler/source example
  path can execute it honestly.
- Request/app resource ownership: MAIN1-07 adds the fixed table and handle safety layer,
  MAIN1-03 adds a minimal native request cleanup boundary, and ENGINE-07 adds app
  lifecycle shutdown plus resource-table-backed request/app cleanup helpers. Provider
  handle ownership policy, leak reports, broader async request-scope retention, and debug
  lifecycle integration remain open.

## Should Fix Soon

- Response/request diagnostics beyond the EPIC-23 safe 500 path, including richer user
  messages for result conversion failures, malformed route params, unsupported methods,
  and app startup failures.
- Route params in runtime handler context beyond MVP strings, including int conversion
  policy and failure diagnostics.
- Query parsing beyond EPIC-23 scalar last-wins values, including repeated-key arrays if
  the public API chooses them.
- Request body parsing skeleton with content-type/body-size policy. MAIN1-04 rejects
  body-bearing requests clearly but does not parse bodies. MAIN1-13 keeps body behavior as
  negative HTTP parser/dispatch coverage until socket-mode conformance has a real
  body-bearing request fixture.
- JSON serialization strategy beyond the current V8 `JSON.stringify` bridge, including
  richer supported value errors, redaction, and benchmark plan.
- Plan module/service sections beyond the MAIN1-03 startup checks. Native validation now
  covers represented route/provider/capability metadata and duplicate provider service
  tokens, but bootstrap module graphs and real service lifetimes still need compiler-emitted
  metadata.
- Stronger provider/capability enforcement for future provider bridges beyond the current
  bridge-ready hook.
- Source map consumption for TypeScript remapping from V8 exceptions. ENGINE-02 emits
  handler-line source maps, but MAIN1-05 still reports generated `app.js` line/column
  because runtime diagnostics do not consume those maps yet. ENGINE-07 did not claim
  source-map remapping; lifecycle/async diagnostics remain stable generated-location
  diagnostics until ENGINE-08 consumes maps for real.
- Provider pooling hardening for PostgreSQL and SQL Server: wait policy, health checks,
  drain behavior beyond the current idempotent close, thread-safety contract, and richer
  diagnostics.
- SQLite file database capability policy.
- Docs examples executable path: replace static checks with Sloppy-run examples as soon as
  the runtime path exists.
- Provider bridge layering watchlist: future PostgreSQL, SQL Server, or other native
  bridges must add `src/engine/v8/intrinsics_<provider>.cc` modules and register through
  `intrinsics.cc`; do not let `engine_v8.cc` become a provider-specific bridge file.
- MAIN1-14 benchmark methodology hardening: release-only measured runs, local artifact
  policy, hardware/build metadata, trend policy, and no external comparisons until
  comparable paths exist. This is deferred behind Slop Engine foundation completion.
- GitHub issue cleanup: close implemented child/parent issues, relabel deferred follow-ups,
  and create only deduped MAIN/MAIN.1 issues after the roadmap and cleanup plan are
  approved. EPIC-21 through EPIC-26 implementation issues should not be recreated. The
  strategic replacement is `tools/github/slop-engine-roadmap-issues.json`; apply only after
  review.

## Deferred By Design

- Production HTTP server behavior beyond the dev-only MVP.
- Full response pipeline: streaming, files, redirects, cookies, content negotiation,
  compression, keep-alive tuning, and middleware/result filters.
- Full route table/trie optimization, catch-all routes, optional segments, regex
  constraints, nested route groups, and ambiguity diagnostics beyond MAIN1-04's
  literal-before-parameter precedence policy.
- Full OpenAPI generation, request/response schema emission, examples, and security
  schemes. MAIN1-11 emits only a route skeleton and marks schemas/security as deferred.
- Full validation engine behavior: body/query/headers/route binding, automatic problem
  responses, coercion, arrays, unions, and custom refinements.
- Production database features: migrations, isolation levels, savepoints, blob/date/json
  shape, array support, TLS/options hardening, PostgreSQL COPY/listen/notify, SQL Server
  TVP/bulk copy, and raw SQL escape hatch policy.
- Dynamic module/package loading, package-manager behavior, native plugin ABI, compiler
  plugins, and Node compatibility.
- Multi-worker/process scaling, hot reload, dev watch, inspector/debugger integration, and
  snapshots.
- Performance dashboards, upload jobs, and CI performance regression thresholds.

## Research Needed

- Oxc integration depth: parser-only MVP versus transform/bundling ownership.
- Official TypeScript checking path: `tsgo` or `tsc`, subprocess versus library, and how
  diagnostics merge with extraction diagnostics.
- JavaScript module format and bundling: ESM loading in V8, generated wrapper shape, import
  maps if ever needed, runtime source-map remapping, and app entrypoint semantics beyond the
  EPIC-24 classic bootstrap runtime handoff.
- V8 process shutdown policy and whether process-wide platform teardown is ever attempted.
  Per-engine destroy is defined; global V8 platform teardown remains deliberately deferred.
- Threading model evolution from inline skeletons to real worker threads/libuv posting,
  including owner-thread identity checks.
- Async provider strategy: worker-pool blocking calls versus nonblocking libpq/socket
  integration; cancellation/deadline semantics by provider.
- Cross-platform SQL Server support versus Windows-first ODBC-only policy.
- Filesystem/network capability semantics: path normalization, symlinks, config/env
  access, and honest non-sandboxing language.
- Runtime dependency packaging story for dynamic V8, libpq, SQLite, ODBC, and future
  provider/runtime assets.

## Nice Later

- Docs link checker and semantic stale-doc checker.
- JS syntax parser/linter once the compiler/tooling story can justify one without adding
  package-manager scope.
- Stronger Rust lint configuration after `sloppyc` grows beyond the placeholder CLI.
- Stronger generated-artifact and ignored-file staging checks.
- More diagnostic polish: CLI-wide diagnostic format flags, structured fixes,
  localization, richer related spans, and source-map remapping.
- Fuzz targets for plan JSON, route patterns, HTTP request-head parsing, source maps, and
  compiler extraction.
- Sanitizer matrix for core-only and provider-enabled builds.
- Benchmark samples for documentation once methodology is stable.
- Optional CLI output polish and machine-readable metadata envelopes for all commands.
- Typed service tokens after string-token plan/debuggability is proven.
- Richer module metadata for health checks, jobs, filters, permissions, and package
  manifests.

## Cleanup Candidates

- Reconcile stale GitHub issue labels: many open parent EPICs still say `status:deferred`
  after their scoped child tasks closed.
- Decide whether to close or retitle legacy open tasks that are now superseded by
  EPIC-21 onward.
- Review `docs/project/current-issue-state-audit.md` and
  `docs/project/main-main1-issue-cleanup-plan.md`, then apply approved GitHub issue cleanup
  separately.
- Remove duplicate or contradictory "Current Phase" paragraphs as docs continue to evolve.

## Overengineering Watchlist

- Avoid broad registries beyond the fixed MAIN1-07 resource table until native app graph and
  request-scope ownership require them.
- Avoid generic provider/vtable abstractions beyond documented provider boundaries.
- Avoid macro DSLs or hidden cleanup paths in C.
- Avoid API shape expansion before compiler/runtime integration catches up.
- Avoid performance dashboards or external comparisons before real comparable paths exist.

## Comment Quality Watchlist

- Public APIs need ownership/lifetime/invariant comments.
- Platform, engine, provider, and threading boundaries need rationale comments.
- Remove stale comments when implementation reality changes.
- Do not add comments that merely narrate obvious syntax.

## Completed Cleanup

- Added the ENGINE-01 framework contract source of truth for JS app API, Results, request
  context, async/microtasks, cancellation/deadlines, HTTP, SQLite, capabilities, and
  deferred behavior.
- Added the ENGINE-03 V8 async runtime slice for microtask-only returned Promise
  settlement, rejected/pending Promise diagnostics, owner-thread microtask drains,
  cancellation snapshots, and bounded request-scope cleanup.
- Added JS/TS and Rust standards docs plus zero-dependency language standards scanners.
- Wired JS/TS and Rust standards scanners into `tools/windows/dev.ps1 lint`.
- Added the EPIC-21 `sloppyc` extraction MVP for one-file literal `mapGet` apps, builder
  form, simple route groups, deterministic artifacts, and compiler diagnostics.
- Added the EPIC-22 dev-only `sloppy run --artifacts` MVP with V8-required startup,
  GET route dispatch, `--once` test mode, tiny local HTTP serving, and clear non-V8
  diagnostics.
- Added EPIC-25 experimental local packaging with Windows ZIP creation, Unix TAR staging,
  package manifests, SHA256 checksums, V8 SDK exclusion policy, and outside-checkout ZIP
  smoke validation.
- Added EPIC-26 default cross-platform CI with Windows clang-cl, Linux clang/gcc, macOS
  clang, POSIX standards scanners, optional/manual V8 validation, and explicit provider
  gate reporting.
- Added MAIN1-07 resource lifecycle foundation with `SlResourceId`, `SlResourceTable`,
  generation/stale validation, wrong-kind diagnostics, close/reuse behavior, and cleanup
  callback tests.
