# V8 Runtime Conformance

This directory records executable lane registration.
V8 conformance runs only when the build is configured with `SLOPPY_ENABLE_V8=ON` and a
valid SDK. Default non-V8 gates do not prove this lane.

## Registered Cases

`conformance.v8.runtime_bridge` runs the existing `engine.v8.smoke` executable. It covers:

- classic-script evaluation and handler calls;
- explicit-length string interop, UTF-8 copying, and generated-source diagnostics;
- compile errors, missing/non-callable functions, thrown handlers, unsupported results,
  and invalid result headers;
- Source Map v3 exception primary-span remapping for thrown functions and registered
  handlers, plus generated-location fallback for missing or malformed maps;
- Promise fulfillment, Promise rejection, pending/deadline behavior, and bounded recursive
  microtasks;
- native Time delay Promise settlement through the owner-thread scheduler and inactive
  `__sloppy.time` registration when `stdlib.time` is not active;
- `stdlib.httpclient` feature activation reusing the private `__sloppy.net` bridge for the
  first outbound HTTP/1.1 client surface;
- request context method/header/body/signal/deadline materialization;
- request-scope cleanup across sync and async outcomes;
- registered handler validation and app-eval rollback.

`conformance.v8.owner_thread` runs the existing `engine.v8.owner_thread` executable. It
proves wrong-thread eval, wrong-thread async handler calls, and wrong-thread destroy avoid
entering the V8 isolate.

`conformance.v8.native_async_scheduler` runs the existing V8 async scheduler executable.
It proves native completion fulfillment/rejection through the owner-thread scheduler and
wrong-thread drain rejection before V8 entry.

`conformance.v8.http_dispatch_execution` runs the existing HTTP dispatch integration
executable. It proves synthetic HTTP dispatch through V8 handlers, including non-GET
methods, headers, JSON body materialization, missing function diagnostics, and thrown
handler diagnostics.

`conformance.hello.run_once`, `conformance.request_context.run_once`, and
`conformance.async_handler.run_once` continue to prove compiled artifact execution through
`sloppy run --artifacts --once` when V8 is configured.

Provider bridge cases are separate from native provider correctness:

- `conformance.sqlite.bridge` proves SQLite JS bridge open/exec/query/queryOne,
  transaction callback settlement, capability-gated resource use, and typed value
  materialization including blob result/`Uint8Array` parameter conversion.
- `conformance.postgres.bridge_live` proves PostgreSQL JS bridge behavior only when the
  Docker-backed live-provider lane and `SLOPPY_POSTGRES_TEST_URL` are configured.
- `conformance.sqlserver.bridge_live` proves SQL Server JS bridge behavior only when the
  Docker-backed live-provider lane, ODBC driver, and async driver support are configured.

## Boundaries

This lane is not default non-V8 evidence, arbitrary bundler source-map support, async stack
remapping evidence, package evidence, live-provider evidence, production-edge HTTP
evidence, benchmark evidence, Node/npm compatibility, or public release readiness. Missing
SDK, Docker, service configuration, ODBC driver support, or SQL Server async-driver support
means skipped/not configured/unavailable, not passed.
