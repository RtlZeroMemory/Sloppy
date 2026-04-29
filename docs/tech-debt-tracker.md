# Technical Debt Tracker

This tracker records real deferred work after the EPIC-00 through EPIC-20 roadmap batch.
It is not a wishlist for random expansion; items here are known gaps between current repo
state and an honest public-alpha path.

## Must Fix Before Public Alpha

- Real `sloppyc` extraction MVP: parse/import public API source, discover
  `Sloppy.createBuilder` / `Sloppy.create`, routes, groups, modules, handlers, and emit
  deterministic `app.plan.json` plus `app.js`.
- Public API to plan emission: bootstrap route/module/service/data metadata must become
  compiler-produced plan metadata instead of in-memory debug snapshots.
- `sloppy run` MVP: load source or build artifacts, start a local dev HTTP server, route
  GET requests, call the V8 handler, and return text/JSON responses.
- HTTP response writer: status, headers, content type, body writing, basic error handling,
  and response descriptor conversion from current `Results.*` shapes.
- Request context model: route params, query params, request metadata, services/config/log,
  and a documented lifetime boundary.
- V8 module loading and bootstrap runtime: load `stdlib/sloppy` reliably, define the bare
  import story, load app module entrypoint, bind intrinsics, and reject unsupported module
  shapes with diagnostics.
- V8 SDK distribution/release packaging: decide linked/bundled strategy, manifest,
  checksums, dynamic library copy policy, and release validation.
- Capability enforcement: declared capabilities must gate provider/filesystem/network
  access before public docs imply a security model.
- Live DB test infrastructure for PostgreSQL and SQL Server: opt-in local env vars are not
  enough for release confidence.
- Cross-platform CI: Linux clang/gcc, macOS clang, Windows clang-cl, and explicit
  V8/provider-gated matrix behavior.
- Release packaging: Windows zip, Linux tar, macOS tar, checksums, install smoke, and
  outside-checkout validation.
- Public alpha docs/examples: at least one executable hello and one executable SQLite demo
  must run through the real Sloppy toolchain, not Node/static checks.

## Should Fix Soon

- Response/request diagnostics for missing handlers, result conversion failures, malformed
  route params, unsupported methods, and app startup failures.
- Route params in runtime handler context, including int parsing policy and failure
  diagnostics.
- Query parsing and route/query percent-decoding policy.
- Request body parsing skeleton with explicit limits and unsupported-content diagnostics.
- JSON serialization strategy for `Results.json`, including supported values, errors,
  redaction, and benchmark plan.
- Plan route/module/provider/capability sections with golden fixtures and native startup
  validation.
- Source map strategy for compiler output and V8 exception remapping.
- Provider pooling hardening for PostgreSQL and SQL Server: wait policy, health checks,
  close/drain behavior, thread-safety contract, and diagnostics.
- SQLite file database capability policy.
- Docs examples executable path: replace static checks with Sloppy-run examples as soon as
  the runtime path exists.
- Benchmark methodology hardening: release-only measured runs, local artifact policy,
  hardware/build metadata, and no external comparisons until comparable paths exist.
- GitHub issue cleanup: close implemented child/parent issues, relabel deferred follow-ups,
  and add EPIC-21 onward issues after the next roadmap is approved.

## Deferred By Design

- Production HTTP server behavior beyond the dev-only MVP.
- Full response pipeline: streaming, files, redirects, cookies, content negotiation,
  compression, keep-alive tuning, and middleware/result filters.
- Full route table/trie optimization, catch-all routes, optional segments, regex
  constraints, nested route groups, route precedence policy, and ambiguity diagnostics.
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
  rewriting, source-map fidelity, and app entrypoint semantics.
- V8 process shutdown policy and whether process-wide platform teardown is ever attempted.
- Threading model evolution from inline skeletons to real worker threads/libuv posting,
  including owner-thread identity checks.
- Async provider strategy: worker-pool blocking calls versus nonblocking libpq/socket
  integration; cancellation/deadline semantics by provider.
- Cross-platform SQL Server support versus Windows-first ODBC-only policy.
- Filesystem/network capability semantics: path normalization, symlinks, config/env
  access, and honest non-sandboxing language.
- Release packaging format and dependency story for V8, libpq, SQLite, ODBC, and future
  stdlib assets.

## Nice Later

- Docs link checker and semantic stale-doc checker.
- Stronger generated-artifact and ignored-file staging checks.
- More diagnostic renderers: JSON, source frames, structured fixes, and localization.
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
- Update `tools/github/create-issues.ps1` with an explicit `-Input` parameter only after
  the team wants alternate issue-data files to be first-class.
- Remove duplicate or contradictory "Current Phase" paragraphs as docs continue to evolve.

## Overengineering Watchlist

- Avoid broad registries until the native app graph and resource table need them.
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

- None in this consolidation PR; this pass records and organizes the backlog without
  implementing runtime features.
