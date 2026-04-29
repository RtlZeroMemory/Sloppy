# Technical Debt Tracker

## Active debt

- V8 SDK source-build workflow exists for local maintainer use, but prebuilt hosting,
  checksums, and update cadence are still deferred.
- Sanitizer coverage is incomplete on Windows.
- Runtime execution is still limited to handwritten V8 smoke artifacts; no HTTP app host,
  compiler output loader, or public TypeScript API exists yet. Native SQLite and
  PostgreSQL providers exist, but they are synchronous and not yet integrated with JS
  resource IDs, app-host disposal, permissions, app-plan entries, or worker-pool execution.
- `SlLoop` exists only as a caller-backed, single-threaded completion queue skeleton; it has
  no libuv/backend integration, OS wakeup mechanism, cross-thread posting, or owner-thread
  enforcement yet.
- `SlAsync` exists only as a caller-owned native settlement skeleton over `SlLoop`; it has
  no V8 Promise integration, microtask policy, request-scope retention, thread-safe
  settlement, cancellation token, deadline, backpressure, or worker-pool integration yet.
- `SlWorkerPool` exists only as an inline/fake design skeleton over `SlLoop`; it has no real
  threads, OS wakeups, thread-safe completion posting, libuv backend, cancellation,
  deadlines, backpressure, blocking DB/filesystem integration, or V8/HTTP integration yet.
- `SlRoutePattern` exists only as a one-pattern parser/matcher foundation. It has no HTTP
  parser/server integration, method dispatch, route table/trie, route precedence,
  catch-all/optional/regex constraints, percent decoding, route groups, OpenAPI metadata,
  validation/schema integration, public TypeScript API, or compiler extraction yet.
- `SlHttpRequestHead` exists only as a complete-buffer HTTP/1 request-head parser over
  llhttp. It has no streaming parser API, body parsing, chunked transfer handling,
  keep-alive state, TCP server, socket I/O, response writer, request pipeline, route
  dispatch, middleware, public TypeScript API, asterisk-form/absolute-form target handling,
  query parsing, percent decoding, or V8/app host integration yet.
- `sl_http_dispatch_request_head` exists only as synthetic in-memory GET dispatch over a
  manual borrowed route binding table. It has no production route table/trie, route
  precedence, plan route section, route params in handler context, HTTP response writer,
  request context, middleware, TCP server, sockets, body parsing, public TypeScript API, or
  compiler extraction yet.
- The bootstrap stdlib now has the bounded EPIC-13 `Results.*` helper set, `schema`,
  route groups, route metadata storage, `Sloppy.create`,
  `Sloppy.createBuilder`, builder build/freeze behavior, structural `app.freeze`, object
  config, memory logging, string-token singleton/transient services, in-memory
  `app.mapGet` facade behavior, the EPIC-14 bootstrap `Sloppy.module` /
  `builder.addModule` skeleton with dependency ordering, services/routes phases,
  attribution, and module debug metadata, and the EPIC-15 data/capabilities foundation with
  database capability metadata, query template lowering, fake data providers, and
  transaction callback semantics. EPIC-16 adds `data.sqlite` metadata/open shape, but the
  stdlib still has no JavaScript-to-native SQLite bridge. It still has no handler
  registration, runtime intrinsic binding, module resolver, compiler import rewriting,
  package-manager behavior, native app-host validation, app run/listen, HTTP response
  conversion, automatic validation, JavaScript database connections, SQL execution from
  JavaScript, or `app.plan.json` emission yet.
- The native SQLite provider exists and is covered by C tests, but it is synchronous and
  not yet connected to JS resource IDs, app-host service disposal, permission enforcement,
  app-plan provider entries, or worker-pool/off-thread execution.
- The native PostgreSQL provider exists and is covered by default non-live C tests plus
  `SLOPPY_POSTGRES_TEST_URL` gated live tests, but it is synchronous and not yet connected
  to JS resource IDs, app-host service disposal, permission enforcement, app-plan provider
  entries, or worker-pool/off-thread execution. Its pool is a bounded skeleton only.
- `examples/hello/` and `examples/ergonomics/` exist as static bootstrap API-shape
  examples. `examples/modules-basic/` exists as a static bootstrap module API-shape
  example. `examples/data-foundation/` exists as a static data/capabilities API-shape
  example. They are not compiled by `sloppyc`, do not emit `app.plan.json`, do not run
  through `sloppy run`, and are not served by an HTTP server.

## Deferred decisions

- `sloppyc`/Oxc integration is planned but not implemented.
- Linux/macOS CI is future.
- Package compatibility is future.
- Exact event loop backend integration is future.
- Thread-safe completion posting is future work.
- `SlLoop` owner-thread identity checks are future work.
- libuv event-loop backend integration is future work; TASK 10.B only proves dependency
  linkage with a local loop init/close smoke.
- Real worker-pool threads are future work.
- Thread-safe worker-pool completion posting into `SlLoop` is future work.
- Worker-pool cancellation, deadlines, and backpressure are future work.
- Worker-pool DB provider integration is future work.
- V8 Promise settlement through `SlAsync`/`SlLoop` is future work.
- V8 microtask draining policy for async handlers is future work.
- Request scope retention until async settlement is future work.
- Thread-safe async settlement/posting is future work.
- Cancellation token, deadline, and backpressure integration with `SlAsync` is future work.
- HTTP request scope integration with `SlScope` is future.
- Resource-table cleanup callbacks registered through `SlScope` are future.
- Async cancellation and deadline-triggered `SlScope` cleanup are future.
- Exact worker-pool implementation is future.
- DB provider async strategy is future.
- DB transaction scope integration with native lifetime cleanup is future.
- Multiple worker/process scaling model is future.
- Cancellation semantics by provider are future.
- Docs freshness automated checker is lightweight and should become more semantic over time.
- Public docs example test runner is future.
- Golden update workflow is future.
- Docs link checker is future.
- Diagnostics JSON output is future.
- Diagnostics source-frame rendering is future.
- Diagnostics source-map integration is future.
- Diagnostics localization is future.
- Diagnostics structured fixes/metadata are future.
- Diagnostics redaction policy is future; TASK 04.A only provides an explicit
  `<redacted>` placeholder helper.
- Plan file-based loading is future work.
- Plan runtime compatibility checks for target platform, target engine, runtime minimum
  version, and stdlib version are future work.
- Plan hash verification is future validation work.
- Plan source map parsing and JSON pointer/source-frame diagnostics are future work.
- Plan route/service/module/data provider/capability/permission sections are future work.
- Plan route/service/module/data provider/capability/permission golden fixtures are future
  work and should be added only when those sections are implemented.
- Exact V8 SDK prebuilt hosting, checksum manifest, and update cadence are future work.
- Exact V8 SDK debug/release packaging matrix is future work.
- Exact V8 dynamic DLL copy/package rules are future work.
- Exact final V8 library list for non-monolithic/component builds is future work.
- V8-backed handwritten `app.plan.json` + `app.js` execution exists only as a smoke path;
  compiler output loading, handler registration intrinsics, HTTP/request context, routing,
  and full result conversion remain future work.
- Explicit V8 process shutdown policy is future work; the bridge keeps process-wide V8
  platform state alive after first initialization and releases only per-engine isolates.
- `SlEngine` owner-thread checks are future bridge/event-loop work.
- Real `sl_engine_call_handler` execution, engine-owned handler tables, and registration
  intrinsics are future EPIC-08/09 work.
- V8 source-map remapping is future work; TASK 07.D reports generated JavaScript locations
  only.
- V8 route/handler-aware diagnostics are future EPIC-08/09 work after plan handler mapping
  exists.
- V8 promise rejection policy and async stack traces are future event-loop/promise work.
- Rich V8 code frames are future diagnostics renderer work.
- ESM/module loading and resolver behavior are future work.
- Route table/trie or equivalent optimized dispatch is future work.
- Route precedence and ambiguity diagnostics are future work.
- Route catch-all/wildcard parameters are future work.
- Route optional segments and regex constraints are future work.
- Nested route groups are future work.
- Route percent decoding and URL normalization policy are future work.
- Production route method dispatch, route table/trie construction, and route precedence are
  future work.
- HTTP server/socket integration with llhttp/libuv is future work after TASK 10.B.
- HTTP streaming parser state is future work.
- HTTP request body parsing is future work.
- HTTP chunked transfer handling is future work.
- HTTP keep-alive state is future work.
- HTTP response writing is future work.
- HTTP response conversion/writing after handler result is future work.
- Route params passed into handler context are future work.
- Plan-owned route section and route-to-handler validation are future work.
- Middleware/request pipeline integration is future work.
- HTTP asterisk-form and absolute-form request target policy is future work.
- HTTP query parsing is future work.
- HTTP percent decoding is future work.
- Public `app.mapGet` native route/API integration beyond in-memory registration is future
  work.
- Route OpenAPI metadata, schema extraction, automatic validation responses, request
  binding, and full validation engine behavior are future work.
- Executable ESM/V8 tests for `stdlib/sloppy` are future work; TASK 11.B/11.C adds only a
  static CTest API-shape check because current V8 smoke execution does not load ESM
  modules.
- V8-backed ESM tests for `stdlib/sloppy` remain future work. TASK 12 adds optional
  executable ESM coverage through `node` when available, but that is not a runtime module
  loading test or a Node compatibility promise.
- `examples/hello/` should become executable through the Sloppy runtime once module
  loading, compiler extraction, plan emission, and HTTP serving exist.
- `app.run`, `app.listen`, `app.build`, native app graph validation, and native app graph
  freeze are future app-host work.
- Config file, environment variable, command-line, secret-manager, validation, and
  redaction providers are future app-host work.
- Console, file, native, timestamped, async, structured-export, scoped, and redacted
  logging are future app-host work.
- Request-scoped service lifetime, disposal hooks, async factories, dependency graph
  validation, cycle diagnostics, typed tokens, decorators, and module-owned services are
  future app-host work.
- Automatic compiler extraction and `app.plan.json` emission from `app.mapGet` are future
  work.
- Compiler extraction of `Sloppy.module(...)`, module dependencies, module service
  tokens, and module route contributions is future work.
- Real `app.plan.json` module emission is future work; TASK 14 exposes only bootstrap
  debug metadata.
- Module package distribution, optional dependencies, module version ranges, and native
  plugin modules are future work.
- Data provider modules are future work; the modules-basic example uses only fake
  in-memory JavaScript services.
- JavaScript-to-native SQLite provider binding is future work; the current native provider
  executes SQL in C tests and the fake data provider remains for JS tests/examples.
- SQL Server provider is future work.
- Production database connection pooling, migrations, file DB capability policy, blob
  support, async worker offload, cancellation/deadline propagation, isolation levels,
  savepoints, PostgreSQL TLS/options hardening, JSON/array support, raw SQL escape hatch,
  and provider-specific quoting remain future data-provider work.
- Capability enforcement, filesystem capabilities, network capabilities, permission grants,
  `sloppy audit`, and app-plan data provider/capability emission remain future work.
- Compiler import rewriting for `"sloppy"` is future work.
- Runtime intrinsic binding for `stdlib/sloppy/internal/intrinsics.js` is future work.

## Cleanup candidates

- Add scanner fixtures or self-test mode for structural checks.
- Decide whether `include/sloppy/os.h` is public or internal before platform APIs grow.
- Expand docs freshness checks to catch broken links and implemented API drift.

## Overengineering Watchlist

- Watch for unnecessary registries.
- Watch for macro DSLs.
- Watch for provider/vtable abstractions before provider phase.
- Watch for generic containers before concrete needs.
- Watch for C code that hides cleanup/error paths.

## Comment Quality Watchlist

- Watch for AI-noise comments.
- Watch for stale rationale comments.
- Watch for missing ownership comments on public APIs.
- Watch for tricky C code without invariants documented.

## Repeated review findings

- None recorded yet.

## Proposed mechanical checks

- V8 leakage scanner expansion once the bridge exists.
- Allocator/resource misuse checks once allocator and resource-table modules exist.
- Docs drift checks for roadmap/source-doc links.
- Public docs example tests once public API exists.
- Golden update intent check once golden harnesses exist.
- Diagnostic snapshot update intent check once more diagnostic fixtures exist.
- Complexity warnings for one-call-site abstractions and high nesting if a reliable scanner
  becomes practical.

## Completed cleanup

- None recorded yet.
