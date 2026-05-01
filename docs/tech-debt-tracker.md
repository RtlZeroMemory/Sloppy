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

- Post-core primitive adoption follow-up:
  `docs/project/post-core-mvp-memory-string-audit.md` records provider-specific cleanup
  outside the completed consolidation/audit work.
  <a id="postgresql-provider-copy-helpers"></a>PostgreSQL cleanup should move
  `src/data/postgres.c` local copy helpers `sl_pg_copy_str`, `sl_pg_copy_cstr`,
  `sl_pg_safe_config_hint`, `sl_pg_copy_columns`, and pool connection-string copies toward
  shared arena copy/C-string boundary helpers. `sl_pg_param_text` integer/float formatting
  still uses `snprintf` into local fixed buffers; add a shared bounded numeric formatting
  helper before rewriting that path. Leave direct libpq C-string adapters in provider
  boundary code until the shared helper exists.
  <a id="sqlserver-odbc-redaction"></a>SQL Server cleanup should move
  `src/data/sqlserver.c` local copy helpers `sl_sqlsrv_copy_str`, `sl_sqlsrv_copy_cstr`,
  `sl_sqlsrv_safe_config_hint`, doctor-result copies in `sl_sqlsrv_set_doctor`, pool
  connection-string copies, and streamed text accumulation in `sl_sqlsrv_append_chunk` /
  `sl_sqlsrv_copy_streamed_text` toward shared arena copy, C-string boundary, and bounded
  string-builder primitives. Keep ODBC handle adapters, `SQLGetData` streaming boundaries,
  and provider-specific connection-string parsing/redaction local unless a shared redaction
  primitive is explicitly scoped.
  <a id="sqlite-v8-param-preflight"></a>SQLite V8 parameter conversion now preflights JS
  array length before reserving native vectors; #431 is handled by the bridge cap and
  V8-gated regression coverage. Future SQLite work should only revisit the cap if the
  documented provider limit changes.
  <a id="tests-strcpy-boundary"></a>Test-only C-string boundary helpers should either
  use shared checked helpers or document why a local boundary helper is acceptable.
- Post-core boundary follow-up:
  `docs/project/post-core-mvp-boundary-audit.md` records the current libuv boundary state.
  HARDEN-01.A retires the legacy public `sl_http_libuv_smoke` helper and routes the
  dev-only CLI server path through `SlHttpTransportServer`; HTTP-25.A/B/C adds bounded
  sequential keep-alive, HTTP-25.D/E adds chunked decoding and internal streaming, and
  HTTP-25.F adds bounded stress/conformance evidence. Remaining HTTP transport debt is
  future owner-approved production hardening or public streaming API work.
- Compiler/runtime completion for realistic supported Sloppy apps beyond ENGINE-02 and
  ENGINE-03: COMPILER-30 (#460/#461-#470) now owns compiler inference for the supported
  Slop app subset: routes, route groups, function modules, providers, config keys, request
  binding, schemas, Results, effect summaries, capabilities, source locations, diagnostics,
  and Plan completeness. Normal repository/service patterns should not require manual
  `uses` metadata when effects are statically resolvable. Runtime work remains separate:
  COMPILER-30.A establishes module boundaries, the compiler library API, and fixture/
  golden harness. COMPILER-30.B/C moves the first parser/resolver/symbol/DSL/static-eval
  contracts into named modules while keeping artifact extraction compatibility in
  `sloppyc.rs`; COMPILER-30.D covers Minimal API route methods, nested literal route
  groups, function-module route contributions, duplicate route validation, and module
  source locations. COMPILER-30.E covers supported SQLite provider registration/lookup
  metadata, config reads, schema declarations, request bindings, and preliminary
  `Results.*` response metadata. Effect/capability inference, validation/completeness, and
  strong Plan logic still need later task slices. Cache reuse, broader async source shapes,
  dispatch/runtime behavior, and provider/capability enforcement must not be hidden inside
  compiler planning.
- Full scalable async runtime beyond ENGINE-03: ENGINE-12.AB adds the first bounded
  `SlAsyncLoop` backend abstraction, libuv backend, and owner-thread V8 continuation
  scheduler. ENGINE-12.CD adds the deterministic provider-executor/cancellation/
  deadline/shutdown/backpressure policy layer and native tests, but full SQLite async
  runtime conversion, HTTP-specific disconnect/shutdown integration, stress evidence,
  source-remapped async diagnostics, and provider-backed interruption remain follow-ups.
  ENGINE-03 covers
  returned Promises that settle during the owner-thread microtask drain, rejection
  diagnostics, pending-Promise failure, cancellation snapshots, and request-scope cleanup
  for the bounded call.
- Provider execution runtime beyond ENGINE-23.A/B/C/D/E/F/G/H serialized and blocking-pool
  admission/execution:
  provider operation descriptors with owned inputs, per-provider-instance bounded
  admission, serialized SQLite-class blocking execution, bounded blocking-pool execution,
  generic cancellation/timeout/shutdown terminal handling, late-completion cleanup-only
  behavior, capability-gated dispatch, redacted diagnostics/counters, bounded stress
  smoke, and the provider runtime integration guide now exist. ENGINE-17 still must route
  SQLite through this executor before claiming scalable SQLite provider execution, and
  future PostgreSQL/SQL Server bridges must consume the same runtime model instead of
  bypassing it.
- Proper HTTP runtime backend beyond ENGINE-04: ENGINE-13.A/B/C now owns the first
  listener/backend architecture, connection and request lifecycle, parser limits,
  timeout/deadline hooks, bounded admission/backpressure, and deterministic lifecycle
  diagnostics. ENGINE-13.D/E adds a bounded backend body reader, supported JSON/text media
  policy, body-read cancellation/timeout/shutdown cleanup paths, shutdown rejection for new
  request work, active request shutdown cancellation hooks, and stable shutdown diagnostics.
  ENGINE-13.F adds bounded deterministic stress/conformance smoke for the implemented core
  parser/lifecycle/body-policy/overload/shutdown/dispatch paths without benchmark or
  production-edge claims. ENGINE-24.A/B adds the first reusable transport listener
  foundation with Slop-owned config/state, libuv-isolated bind/listen/accept, bounded
  accepted-connection placeholders, overflow close, and stop/dispose cleanup. ENGINE-24.C
  adds the accepted-connection read loop and bounded request accumulation for one parsed
  Content-Length request-ready state. ENGINE-24.D adds dispatch/write/close-after-response
  over a narrow internal dispatch callback and the existing response writer. ENGINE-24.E
  adds transport disconnect cancellation, header/body/request/write timeout hooks,
  deterministic 408 timeout responses where safe, shutdown rejection, active connection
  close, and cleanup-once terminal paths. ENGINE-24.F/#417 is now bounded localhost TCP
  smoke/conformance evidence only. ENGINE-24.G/#418 records the explicit MVP
  close-after-response decision and defers HTTP/1.1 keep-alive, pipelining, chunked
  request decoding, and streaming response writing. HTTP-25.A/B/C adds bounded sequential
  keep-alive with no pipelining, idle timeout, max requests, lifecycle reset, and shutdown
  close policy. HTTP-25.D/E adds bounded chunked request decoding and the first
  internal/native chunked response writer. HTTP-25.F adds bounded keep-alive/streaming
  stress and conformance evidence. Remaining HTTP transport debt is public
  request/response streaming APIs if owner-approved, production graceful-drain policy,
  production hardening, and middleware policy if ever scoped. This
  is separate from ENGINE-12 because HTTP has parser, connection, body, and shutdown policy
  that sits above generic async completions.
- Module/bootstrap completion: ENGINE-14 owns stdlib/bootstrap asset loading, app module
  loading, ESM/classic decision, module cache, import rewrite and intrinsic boundaries,
  source names, reload/dev-loop implications, and V8 startup diagnostics.
- Source maps and diagnostics completion: ENGINE-15 owns compiler maps, generated
  artifact mapping, V8 exception remapping, async diagnostic JSON/source frames, redaction,
  stable codes, CLI diagnostic format, and diagnostic goldens.
- App/resource lifetime runtime: ENGINE-16 owns app startup/shutdown, request/app scopes,
  resource cleanup, cancellation propagation, leak-oriented hooks, and lifecycle
  diagnostics.
- SQLite runtime hardening beyond ENGINE-05: JS transactions/prepared-statement decision,
  file database policy, ENGINE-23 serialized blocking offload, cancellation/deadline
  interruption beyond pre-call checks, request-scope automatic cleanup/leak reporting, and
  richer production conformance.
- CLI/dev loop runtime: ENGINE-18 owns `sloppyc`/`sloppy run` UX, source-input run
  decision, artifact inspection, doctor, audit, OpenAPI route skeleton policy, watch/dev
  decision, and command diagnostics.
- Framework configuration follow-up: FRAMEWORK-01.B implements the first config model, but
  reload-on-change, user secrets, custom/remote providers, broad arbitrary CLI config
  binding, native typed Plan graph consumption, doctor/OpenAPI config reporting, request
  binding, validation, and PostgreSQL/SQL Server JS provider config bridges remain
  deferred. Strong Plan #355-#359 should promote emitted config metadata into the typed
  graph before tooling claims deeper config awareness.
- Framework API shape migration: the current proven stdlib/compiler examples still use
  `mapGet`/builder/data shorthand shapes in several places. The locked post-Core target in
  `docs/project/framework-api-shape.md` is Minimal API `app.get/post/...`, function
  modules, explicit provider imports, generated capabilities, and Plan-visible config.
  Future implementation PRs must migrate code/tests/examples deliberately instead of
  mixing target examples with executable claims.
- Conformance compatibility suite: ENGINE-19 owns compiler to Plan to runtime to V8 to
  HTTP evidence, async/body/header/SQLite/capability/lifecycle/package cases, and default
  versus optional gate reporting.
- ENGINE-19.A conformance matrix is now documented in
  `docs/project/engine-19-conformance-matrix.md` with a small static CMake check. The
  ENGINE-19.BC V8/HTTP/async slice registers existing executable coverage under
  matrix-aligned CTest names for default synthetic HTTP dispatch, localhost transport MVP,
  native async/backend semantics, and V8-gated runtime/HTTP/async behavior. ENGINE-19.D
  registers the existing SQLite provider, capability registry, provider-executor denial,
  V8 SQLite bridge, denied-capability, and users API localhost transport proof under
  matrix-aligned SQLite/capability labels. ENGINE-19.E hardens the local package
  outside-checkout smoke to validate extracted-package CLI startup, required files/stdlib
  assets, packaged `sloppyc build`, and honest non-V8 artifact-execution skip reporting.
  The remaining ENGINE-19 debt is consolidation/audit, not broader runtime behavior hidden
  inside conformance PRs.
- Strong Plan strategic layer: ENGINE-20 owns typed route/handler/capability/provider/
  artifact graphs, static validation, compatibility, doctor/audit, future OpenAPI and
  optimization hooks, versioning, and internal tooling leverage. It consumes COMPILER-30
  output rather than reimplementing compiler inference.
- Memory and string runtime follow-through: ENGINE-21.A/B/C/D/E/F now provide the primitive
  layer for app/request/temp/static lifetime rules, allocation policy, string/byte views,
  arena-owned copies, byte and string builders, formatting utilities, bounded app/static
  string interning/symbol tables, focused safety/stress tests, private V8/native string
  conversion helpers, and SQLite text/blob ownership helpers.
- Memory/string adoption and hot-path refactor: ENGINE-22 owns adoption across HTTP
  request parse/response write/body buffering, V8 conversions, SQLite row/result/parameter
  conversion, diagnostics/source frames/JSON, Plan/artifact loading, stable metadata
  lookup, CLI output, and allocation-aware conformance/benchmark guards. ENGINE-22.A
  covers the current complete-buffer HTTP parser/body/response/route hot paths, and
  ENGINE-22.C covers the current Plan parser, `sloppy run --artifacts` bundle/source-map
  loader path, and stable parsed-Plan metadata interning. ENGINE-22.D covers
  provider-neutral V8 bridge string adoption. ENGINE-22.E covers SQLite result/parameter
  ownership adoption for the current native provider and V8 bridge. ENGINE-22.F covers the
  non-SQLite cleanup pass for capability denial hint construction, OpenAPI path skeleton
  normalization, and the low-capacity denial-hint regression guard. Backend ownership,
  remaining CLI output, and allocation-aware conformance/benchmark guards remain open.
- SQLite end-to-end: ENGINE-17.E now proves the public JS handler path through native
  SQLite provider calls, capability enforcement, request body handling, response JSON, and
  executable users API conformance over localhost TCP. Remaining debt is cancellation-aware
  async/offload operation boundaries, request-scope automatic provider ownership/leak
  reporting, and production hardening beyond the proof fixture.
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
- Source-input follow-up after ENGINE-02.E: `sloppy run <source.js>` and `sloppy run`
  through `sloppy.json` now compile through `sloppyc`, validate artifacts, and reuse the
  `--artifacts` path. Remaining debt is cache reuse/stale-cache rejection, TypeScript
  lowering, relative imports/function modules, richer source-map diagnostics, and
  watch/dev-loop policy.
- HTTP production response pipeline beyond ENGINE-13.A/B/C/D/E/F,
  ENGINE-24.A/B/C/D/E/F/G, HTTP-25.A/B/C/D/E/F, and the ENGINE-17.E users API proof: redirect
  helpers, streaming/files, cookies, content negotiation, hardening beyond #446 bounded
  keep-alive/streaming stress and conformance,
  graceful drain behavior beyond immediate-cancel/drain-lite transport shutdown, broader
  V8 transport conformance, and production error pages.
- Request context model beyond ENGINE-04: typed/coerced route/query/body binding,
  services/config/log injection, and real request-scoped lifetime boundaries.
- V8 module loading beyond EPIC-24: true ESM loading, production module cache, richer source
  maps, real public async sources, HTTP disconnect/shutdown cancellation, and executable
  public examples through the final bootstrap module shape remain open. ENGINE-03 adds
  microtask-only Promise settlement for direct async handlers; ENGINE-12.AB adds native
  test-completion settlement through the owner-thread scheduler. The remaining ENGINE-12
  policy/evidence tasks must land before Sloppy makes scalable async, async provider,
  async HTTP lifecycle, or performance claims. The
  compiler rejection `SLOPPYC_E_UNSUPPORTED_ASYNC_HANDLER_BODY` remains valid for `await`,
  multi-statement async bodies, and non-direct async returns until those shapes become
  executable.
- MAIN1-12 package/CI hardening follow-ups: exact dynamic V8 runtime file lists, hosted
  prebuilt SDK source, V8-enabled package execution validation, hosted package CI evidence,
  and stable sanitizer/fuzz jobs remain open.
- SQLite remaining hardening after ENGINE-05: capability-wired open/use and stable result/
  error mapping are implemented for the V8 bridge, ENGINE-17.A/C finalizes the public JS
  open/close/exec/query/queryOne shape and callback transaction policy, and ENGINE-23.C
  provides the serialized blocking executor model, but the bridge is not yet converted to
  that executor. Public file database policy, request-scope automatic provider cleanup,
  cancellation/deadline interruption, and public prepared statement handles remain
  deferred.
- Live DB service infrastructure for PostgreSQL and SQL Server: opt-in local env vars and
  separate skipped CTest gates make reporting honest, but hosted service jobs are still
  needed for release confidence.
- Cross-platform CI hardening: default Linux clang/gcc, macOS clang, Windows clang-cl, and
  explicit V8/provider-gated reporting exist; local V8 SDK discovery is centralized through
  the Windows resolver, but hosted prebuilt SDK/cache setup, optional live provider service
  jobs, sanitizer/fuzz jobs, and package smoke remain open.
- Cross-platform release validation: local Windows ZIP tooling, Unix TAR tooling,
  checksums, outside-checkout package smoke, and packaged compiler smoke exist, but hosted
  Linux/macOS package execution, V8 package execution smoke, release hardening, and CI
  validation remain open after MAIN1-12.
- Public alpha docs/examples: at least one executable hello must run through the real
  Sloppy toolchain. MAIN1-08 adds a real V8-gated SQLite JS-native bridge fixture, but the
  public source-stdlib SQLite tutorial remains deferred until the compiler/source example
  path can execute it honestly.
- Request/app resource ownership: MAIN1-07 adds the fixed table and handle safety layer,
  MAIN1-03 adds a minimal native request cleanup boundary, and ENGINE-07 adds app
  lifecycle shutdown plus resource-table-backed request/app cleanup helpers. Provider
  handle ownership policy, leak reports, broader async request-scope retention, and debug
  lifecycle integration remain open.
- Memory/string adoption completion: #32 is closed as completed by ENGINE-21.C's primitive
  builder surface. ENGINE-21.D and ENGINE-22.A through ENGINE-22.F have also landed for the
  current HTTP, diagnostics/CLI, Plan/artifact, V8, SQLite, and bounded cleanup paths.
  Public alpha should still avoid broad memory/string hot-path claims until future backend,
  provider, and conformance work proves those later paths.

## Should Fix Soon

- Response/request diagnostics beyond the EPIC-23 safe 500 path, including richer user
  messages for result conversion failures, malformed route params, unsupported methods,
  and app startup failures.
- Route params in runtime handler context beyond MVP strings, including int conversion
  policy and failure diagnostics.
- Query parsing beyond EPIC-23 scalar last-wins values, including repeated-key arrays if
  the public API chooses them.
- Request body conformance beyond unit/integration coverage: ENGINE-04 parses bounded
  JSON/text bodies and exposes them to V8 when enabled, ENGINE-13.D/E adds backend body
  reader cancellation/shutdown unit coverage, and ENGINE-13.F adds bounded repeated body
  policy smoke. Socket-mode conformance still needs a body-bearing fixture once that layer
  can drive local HTTP requests deterministically.
- JSON serialization strategy beyond the current V8 `JSON.stringify` bridge, including
  richer supported value errors, redaction, and benchmark plan.
- Plan module/service sections beyond the MAIN1-03 startup checks. Native validation now
  covers represented route/provider/capability metadata and duplicate provider service
  tokens, but bootstrap module graphs and real service lifetimes still need compiler-emitted
  metadata.
- Future provider bridges must route through the ENGINE-23 capability-gated executor
  dispatch path and follow `docs/project/provider-runtime-integration-guide.md` instead
  of using only direct bridge-ready hooks.
- Provider primitive cleanup follow-up is mapped to #448; PostgreSQL/SQL Server JS bridge
  implementation remains deferred and is not part of immediate next-wave work.
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
- V8 bridge layering watchlist: HTTP/framework-specific bridge code belongs in dedicated
  sibling modules such as `src/engine/v8/http_bridge.cc`; provider bridges must add
  `src/engine/v8/intrinsics_<provider>.cc` modules and register through `intrinsics.cc`.
  Do not let `engine_v8.cc` become a feature-specific bridge file.
- MAIN1-14 benchmark methodology hardening: release-only measured runs, local artifact
  policy, hardware/build metadata, trend policy, and no external comparisons until
  comparable paths exist. This is deferred behind Slop Engine foundation completion.
- GitHub issue cleanup follow-through: #295 still needs owner review before closure or
  narrowing. The 2026-04-30 cleanup record is
  `docs/project/post-core-mvp-issue-reconciliation.md`; #26 now has scanner fixture/
  self-test proof in the platform-boundary scanners.

## Deferred By Design

- Production HTTP server behavior beyond the dev-only MVP, ENGINE-13 backend
  state/admission foundation, and ENGINE-24.A/B/C/D/E listener/read/dispatch/write/
  terminal-state foundation.
- Full response pipeline: streaming, files, redirects, cookies, content negotiation,
  compression, keep-alive tuning beyond the bounded HTTP-25.A/B/C defaults, and
  middleware/result filters.
- #433 `HTTP-25: HTTP/1.1 Keep-Alive and Streaming`: HTTP-25.A/B/C implements the bounded
  sequential keep-alive connection loop, idle timeout, max requests per connection,
  lifecycle reset, close policy, and default localhost conformance. HTTP-25.D/E adds
  bounded chunked request decoding, rejected trailers, an internal/native chunked response
  writer, pending-write cap diagnostics, and HTTP-25.F bounded keep-alive/streaming
  stress/conformance. Deferred HTTP-25 work remains any future owner-approved public
  streaming API, SSE/WebSocket/file streaming, or production hardening. No pipelining, concurrent
  requests on one connection, or production-edge HTTP claim is implied.
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
- Threading model evolution beyond provider worker execution: provider-specific
  cancellation/interruption and owner-thread continuation evidence for real provider
  bridges. ENGINE-23 bounded stress smoke covers native executor behavior only, not live
  database throughput.
- Async provider strategy after ENGINE-23: serialized SQLite-class blocking calls and
  bounded blocking pools now have generic terminal/capability semantics plus bounded stress
  smoke; remaining research is nonblocking libpq/socket integration and provider-specific
  native cancellation/interruption semantics.
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

- Reconcile stale GitHub issue labels on closed issues if label hygiene becomes useful for
  reporting.
- Review #295 for async diagnostic JSON scope after the narrow ENGINE-07 contribution.
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

- Applied the 2026-04-30 GitHub issue hygiene pass after ENGINE-23 roadmap creation:
  closed completed ENGINE-01, ENGINE-03 through ENGINE-07, ENGINE-12, ENGINE-21,
  ENGINE-22, and old #32 with evidence comments; documented kept-open and human-review
  issues in `docs/project/post-core-mvp-issue-reconciliation.md` and
  related project audit documents.
- Added the ENGINE-01 framework contract source of truth for JS app API, Results, request
  context, async/microtasks, cancellation/deadlines, HTTP, SQLite, capabilities, and
  deferred behavior.
- Added the ENGINE-03 V8 async runtime slice for microtask-only returned Promise
  settlement, rejected/pending Promise diagnostics, owner-thread microtask drains,
  cancellation snapshots, and bounded request-scope cleanup.
- Added JS/TS and Rust standards docs plus zero-dependency language standards scanners.
- Wired JS/TS and Rust standards scanners into `tools/windows/dev.ps1 lint`.
- Added platform-boundary scanner self-tests for forbidden include detection and
  per-platform allowlisting.
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
- Added ENGINE-05 SQLite end-to-end runtime wiring for V8-gated handlers: Plan provider
  resolution, database capability hook checks, resource-table-owned handles, batched row
  mapping, JSON handler fixture, denied-capability fixture, and fail-closed missing-hook
  behavior.
## ENGINE-14 Follow-ups

- Source-map diagnostics are multi-file at artifact level, but richer runtime remapping and
  code-frame diagnostics remain part of the source-map diagnostics completion track.
- Full V8 native ESM loading, dynamic import, package/module distribution, and
  package-manager behavior remain intentionally deferred.
- Function modules support the framework MVP route shape only: direct app-parameter routes
  and nested literal route groups from source-local imports. Controllers, decorators,
  framework configuration, request binding, validation, Results completion, and example
  hardening remain tracked by the framework follow-up issues.
