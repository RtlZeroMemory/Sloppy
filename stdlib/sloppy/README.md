# Sloppy Bootstrap Stdlib

This directory contains the active bootstrap standard library and staged runtime asset set.
This directory contains the source-controlled JavaScript facade used by bootstrap tests,
examples, compiler fixtures, and staged runtime assets. It is the Sloppy
bootstrap facade for the current runtime surface.

## Layout

```text
stdlib/sloppy/
  index.js
  app.js
  results.js
  schema.js
  testing.js
  testservices.js
  data.js
  fs.js
  http.js
  net.js
  problem-details.js
  request-id.js
  request-logging.js
  time.js
  codec.js
  crypto.js
  os.js
  workers.js
  health.js
  metrics.js
  internal/
    capabilities.js
    config.js
    intrinsics.js
    logging.js
    modules.js
    routes.js
    services.js
    shared.js
    runtime-classic.js
  bootstrap.manifest.json
```

The installed layout is staged under:

```text
lib/sloppy/bootstrap/sloppy/
```

## Current Surface

- `index.js` re-exports the app-host facade, result helpers, problem detail defaults,
  schema helpers, provider
  facades, and core API namespaces.
- `app.js` provides the bootstrap builder/app/module surface used by examples and tests:
  `Sloppy.create()`, `Sloppy.createBuilder()`, `Sloppy.module(...)`, `Router.group(...)`,
  route registration, middleware/filter registration, app-host CORS policy/preflight
  handling, health/readiness/liveness/startup routes, management endpoints, route-only `app.useModule(...)`,
  explicit controller mapping, group metadata, config/log/services/capabilities facades,
  structural freeze behavior, and debug snapshots.
- `internal/capabilities.js`, `internal/config.js`, `internal/logging.js`,
  `internal/modules.js`, `internal/routes.js`, `internal/services.js`, and
  `internal/shared.js` hold app-host implementation helpers. They are staged bootstrap
  assets. Public imports should use `sloppy`.
- `results.js` provides frozen result descriptor helpers. V8-gated runtime conversion is
  handled by the engine/runtime bridge; response writing belongs to runtime conversion.
- `app.js` owns `app.useErrors(...)` and `app.mapError(...)`; `problem-details.js`
  keeps `ProblemDetails.defaults(...)` as the compatibility descriptor for safe
  route-handler error responses.
- `request-id.js` provides `RequestId.defaults(...)` middleware for app-host request
  IDs, trusted incoming ID admission, and optional response headers.
- `request-logging.js` provides `RequestLogging.defaults(...)` middleware that writes
  one structured entry per completed app-host request through the configured logger.
- `health.js` and `metrics.js` provide the first-party operations backend used by
  `app.health()` and `app.management(...)`.
- `schema.js` provides the current `Schema` validation metadata surface for
  strings, numbers, integers, booleans, arrays, enums, literals, optional,
  nullable, defaulted fields, object shapes, and request body validation
  errors.
- `testing.js` provides `TestHost`, `Testing.createHost`, fake clock helpers,
  and test data helpers. App-host mode dispatches in memory through route
  handlers, middleware, results, CORS, health checks, and scoped services.
  Artifact/package modes call the Sloppy CLI so requests enter the native
  runtime path. The dogfood proof in
  `tests/bootstrap/test_prealpha_control_plane_dogfood.mjs` imports the
  `examples/prealpha-control-plane` route modules through this host.
- `testservices.js` provides experimental `TestServices`, the opt-in Docker-backed
  PostgreSQL and SQL Server test service layer. It uses `sloppy/os` process
  APIs for Docker CLI lifecycle, `sloppy/data` provider bridges for readiness
  and SQL helpers, and redacted diagnostics/cleanup for TestHost integration.
- `http.js` provides the first-party outbound HTTP factory: named clients,
  typed clients, resilience policies, service registration metadata, TestHost
  mocks, metrics, diagnostics, and health snapshots over the low-level
  `HttpClient` transport in `net.js`.
- `codec.js`, `crypto.js`, `fs.js`, `time.js`, `net.js`, `os.js`, and `workers.js` expose
  the current public API shape and feature-gated bridge calls where native bridge support
  exists.
- `data.js` exposes query-template lowering, provider metadata helpers, and SQLite,
  PostgreSQL, and SQL Server bridge entry points when the V8 lane installs the matching
  provider bridge with Plan/capability metadata. SQL operation options accept `signal`,
  `deadline`, and `timeoutMs` for Slop-side pre-dispatch cancellation/deadline checks.
- `internal/runtime-classic.js` is the V8-gated classic-script runtime asset loaded before
  generated artifacts. Generated code reads `globalThis.__sloppy_runtime` and registers
  handlers through Sloppy-owned intrinsics.

## Boundaries

- The bootstrap stdlib is JavaScript source staged with the runtime.
- Source examples may use relative imports into this directory when they are API-shape
  fixtures; compiler-owned runnable examples use the supported bare `"sloppy"` input shape.
- Feature-gated APIs fail closed when the active runtime bridge is unavailable.
- SQL operation cancellation/deadline options are Slop-side admission checks unless a
  provider-specific lane separately documents and tests active native interruption.
- Native handles and raw pointers are not exposed to JavaScript. Resource-backed bridge
  facades use opaque Sloppy-owned objects, not public slot/generation fields.
- Node compatibility is limited to explicit `sloppy/node/*` shim modules staged by
  the dependency-loader path. Bun, Deno, browser Web API compatibility, full npm
  ecosystem parity, and runtime package-manager behavior remain separate work.
- Benchmark and release readiness are covered by dedicated docs and gates.

## Future Work

- `app.run`, `app.listen`, and `app.build` belong to later app lifecycle work.
- Public handler registration APIs will be documented when the runtime support lands.
- Full compiler extraction and arbitrary import rewriting beyond the supported
  Sloppy/package resolver subset are compiler/runtime work.
- ORM and schema-management behavior belong to future database provider work.
  Current migrations are filename/hash ordered SQL files for SQLite,
  PostgreSQL, and SQL Server.
- Native plugins and full app lifecycle integration are planned separately.
- Config file/environment/CLI loading inside the JS stdlib itself, secret managers,
  tracing exporters, async service factories, and native service graph
  validation belong to later framework/runtime slices. Native console and JSONL file
  logging sinks are owned by the C runtime and configured through Plan/config metadata.

## Source Docs

- `docs/api/index.md`
- `docs/api/testhost.md` — `TestHost`
- `docs/api/testservices.md` — `TestServices`
- `docs/api/filesystem.md` — `sloppy/fs`
- `docs/api/network.md` — `sloppy/net`
- `docs/api/http-client.md` — `Http` and `HttpClient`
- `docs/reference/http-client.md` — HTTP factory reference
- `docs/internals/http-client-runtime.md` — HTTP client runtime architecture
- `docs/api/os.md` — `sloppy/os`
- `docs/api/time.md` — `sloppy/time`
- `docs/api/crypto.md` — `sloppy/crypto`
- `docs/api/codec.md` — `sloppy/codec`
- `docs/api/workers.md` — `sloppy/workers`
- `docs/contributor/js-ts-standards.md`
- `docs/reference/supported-syntax.md`
- `docs/reference/providers.md`
- `docs/internals/security-model.md`
- `docs/internals/runtime.md`
