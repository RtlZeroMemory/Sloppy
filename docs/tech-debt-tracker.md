# Technical Debt Tracker

This tracker records real deferred work after the EPIC-00 through EPIC-20 roadmap batch.
It is not a wishlist for random expansion; items here are known gaps between current repo
state and an honest public-alpha path.

## Must Fix Before Public Alpha

- Public API to plan emission beyond EPIC-21: bootstrap module/service/data metadata must
  become compiler-produced plan metadata instead of in-memory debug snapshots.
- `sloppy run` source/build handoff: EPIC-22 can load artifacts, but source input still
  needs a clean `sloppyc` handoff/cache story.
- Source-input `sloppy run <source.js>` implementation: MAIN1-01 decided that alpha keeps
  the explicit two-step artifact workflow. Implementing direct source input still needs a
  scoped compiler handoff, cache keys, stale-artifact checks, source diagnostics, and
  rebuild policy.
- HTTP production response pipeline beyond EPIC-23: custom headers, redirect helpers,
  streaming/files, cookies, content negotiation, keep-alive policy, and production error
  pages.
- Request context model beyond EPIC-23: body/header access, typed/coerced route/query
  binding, services/config/log injection, and real request-scoped lifetime boundaries.
- V8 module loading beyond EPIC-24: true ESM loading, production module cache, richer source
  maps, required async/event-loop Promise support, and executable public examples through
  the final bootstrap module shape remain open. MAIN1-05 documents and tests the current
  owner-thread, lifecycle, generated-source diagnostic, and Promise-rejection policies, but
  does not add full async JavaScript. Promise support is deferred, not optional: before
  Sloppy claims async handlers, V8 Promise settlement, microtask policy, request-scope
  lifetime, and rejected-promise diagnostics must be implemented and tested.
- MAIN1-12 package/CI hardening follow-ups: exact dynamic V8 runtime file lists, hosted
  prebuilt SDK source, V8-enabled package execution validation, hosted package CI evidence,
  and stable sanitizer/fuzz jobs remain open.
- Capability enforcement: declared capabilities must gate provider/filesystem/network
  access before public docs imply a security model.
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
- Request/app resource ownership: MAIN1-07 adds the fixed table and handle safety layer, and
  MAIN1-03 adds a minimal native request cleanup boundary, but provider handle wiring,
  leak reports, async request-scope retention, and debug lifecycle integration remain open.

## Should Fix Soon

- Response/request diagnostics beyond the EPIC-23 safe 500 path, including richer user
  messages for result conversion failures, malformed route params, unsupported methods,
  and app startup failures.
- Route params in runtime handler context beyond MVP strings, including int conversion
  policy and failure diagnostics.
- Query parsing beyond EPIC-23 scalar last-wins values, including repeated-key arrays if
  the public API chooses them.
- Request body parsing skeleton with content-type/body-size policy. MAIN1-04 rejects
  body-bearing requests clearly but does not parse bodies.
- JSON serialization strategy beyond the current V8 `JSON.stringify` bridge, including
  richer supported value errors, redaction, and benchmark plan.
- Plan module/service sections beyond the MAIN1-03 startup checks. Native validation now
  covers represented route/provider/capability metadata and duplicate provider service
  tokens, but bootstrap module graphs and real service lifetimes still need compiler-emitted
  metadata.
- Provider/capability enforcement that turns MAIN1-02 metadata into denied-operation
  behavior and diagnostics.
- Source map strategy for compiler output and TypeScript remapping from V8 exceptions.
  MAIN1-05 reports generated `app.js` line/column only because current maps are
  placeholders.
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
  comparable paths exist.
- GitHub issue cleanup: close implemented child/parent issues, relabel deferred follow-ups,
  and create only deduped MAIN/MAIN.1 issues after the roadmap and cleanup plan are
  approved. EPIC-21 through EPIC-26 implementation issues should not be recreated.

## Deferred By Design

- Production HTTP server behavior beyond the dev-only MVP.
- Full response pipeline: streaming, files, redirects, cookies, content negotiation,
  compression, keep-alive tuning, and middleware/result filters.
- Full route table/trie optimization, catch-all routes, optional segments, regex
  constraints, nested route groups, and ambiguity diagnostics beyond MAIN1-04's
  literal-before-parameter precedence policy.
- Full OpenAPI generation, request/response schema emission, examples, and security
  schemes.
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
  maps if ever needed, source-map fidelity, and app entrypoint semantics beyond the EPIC-24
  classic bootstrap runtime handoff.
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
