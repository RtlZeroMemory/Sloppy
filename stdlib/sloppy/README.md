# Sloppy Bootstrap Stdlib

Status: active bootstrap standard library and staged runtime asset set.

This directory contains the source-controlled JavaScript facade used by bootstrap tests,
examples, compiler fixtures, and staged runtime assets. It is not a Node compatibility shim
and does not imply npm/package-manager behavior.

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
  time.js
  codec.js
  crypto.js
  os.js
  workers.js
  internal/
    intrinsics.js
    runtime-classic.js
  bootstrap.manifest.json
```

The installed layout is staged under:

```text
lib/sloppy/bootstrap/sloppy/
```

## Current Surface

- `index.js` re-exports the app-host facade, result helpers, schema helpers, provider
  facades, and core API namespaces.
- `app.js` provides the bootstrap builder/app/module surface used by examples and tests:
  `Sloppy.create()`, `Sloppy.createBuilder()`, `Sloppy.module(...)`, route registration,
  group metadata, config/log/services/capabilities facades, structural freeze behavior,
  and debug snapshots.
- `results.js` provides frozen result descriptor helpers. V8-gated runtime conversion is
  handled by the engine/runtime bridge; descriptors do not write responses by themselves.
- `schema.js` provides the current validation metadata skeleton for strings, numbers,
  booleans, and object shapes.
- `codec.js`, `crypto.js`, `fs.js`, `time.js`, `net.js`, `os.js`, and `workers.js` expose
  the current public API shape and feature-gated bridge calls where native bridge support
  exists.
- `data.js` exposes query-template lowering, provider metadata helpers, and SQLite,
  PostgreSQL, and SQL Server bridge entry points when the V8 lane installs the matching
  provider bridge with Plan/capability metadata.
- `internal/runtime-classic.js` is the V8-gated classic-script runtime asset loaded before
  generated artifacts. Generated code reads `globalThis.__sloppy_runtime` and registers
  handlers through Sloppy-owned intrinsics.

## Boundaries

- The bootstrap stdlib is JavaScript source, not a package-manager distribution.
- Source examples may use relative imports into this directory when they are API-shape
  fixtures; compiler-owned runnable examples use the supported bare `"sloppy"` input shape.
- Feature-gated APIs fail closed when the active runtime bridge is unavailable.
- Native handles and raw pointers are not exposed to JavaScript.
- Node, Bun, Deno, Web API, and npm compatibility are not claimed.
- Benchmark, production-readiness, and public alpha claims do not come from this directory.

## Not Implemented Here

- `app.run`, `app.listen`, or `app.build`;
- public handler registration APIs;
- full compiler extraction or arbitrary import rewriting;
- ORM, migrations, or schema-management behavior for database providers;
- nested route groups, middleware, automatic validation/request binding, module packages,
  native plugins, or full app lifecycle integration;
- config file/environment/CLI loading inside the JS stdlib itself, secret managers, native
  logging sinks, request-scoped service lifetimes, disposal hooks, async factories, or
  typed DI tokens.

## Source Docs

- `docs/developer-ergonomics.md`
- `docs/js-ts-standards.md`
- `docs/compiler-supported-syntax.md`
- `docs/data-providers.md`
- `docs/security-permissions.md`
- `docs/execution-model.md`
