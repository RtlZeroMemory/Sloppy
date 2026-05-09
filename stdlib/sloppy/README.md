# Sloppy Bootstrap Stdlib

This directory contains the active bootstrap standard library and staged runtime asset set.
This directory contains the source-controlled JavaScript facade used by bootstrap tests,
examples, compiler fixtures, and staged runtime assets. It is the Sloppy
bootstrap facade for Sloppy-owned application imports.

## Layout

```text
stdlib/sloppy/
  index.js
  app.js
  results.js
  schema.js
  data.js
  fs.js
  net.js
  problem-details.js
  time.js
  codec.js
  crypto.js
  os.js
  workers.js
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
  route registration, middleware/filter registration, route-only `app.useModule(...)`,
  explicit controller mapping, group metadata, config/log/services/capabilities facades,
  structural freeze behavior, and debug snapshots.
- `internal/capabilities.js`, `internal/config.js`, `internal/logging.js`,
  `internal/modules.js`, `internal/routes.js`, `internal/services.js`, and
  `internal/shared.js` hold app-host implementation helpers. Public imports come from the
  documented package entry points.
- `results.js` provides frozen result descriptor helpers. V8-gated runtime conversion is
  handled by the engine/runtime bridge; response writing belongs to runtime conversion.
- `problem-details.js` provides `ProblemDetails.defaults(...)` descriptors for safe
  route-handler error responses.
- `schema.js` provides the current validation metadata surface for strings, numbers,
  integers, booleans, arrays, optional fields, and object shapes.
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
- Node, Bun, Deno, Web API, and npm compatibility are separate compatibility
  tracks.
- Benchmark, operations, and release readiness are covered by dedicated docs and gates.

## Current Boundaries

- `app.run`, `app.listen`, and `app.build` belong to later app lifecycle work.
- Public handler registration APIs will be documented when the runtime support lands.
- Full compiler extraction and arbitrary import rewriting are compiler/runtime work.
- ORM, migrations, and schema-management behavior belong to future database provider work.
- Module packages, native plugins, and full app lifecycle integration are planned separately.
- Config file/environment/CLI loading inside the JS stdlib itself, secret managers, native
  logging sinks, async service factories, typed DI tokens, and native service graph
  validation belong to later framework/runtime slices.

## Source Docs

- `docs/api/README.md`
- `docs/contributor/js-ts-standards.md`
- `docs/reference/supported-syntax.md`
- `docs/reference/providers.md`
- `docs/internals/security-model.md`
- `docs/internals/runtime.md`
