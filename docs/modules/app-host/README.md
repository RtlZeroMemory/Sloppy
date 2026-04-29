# App Host Module

## Status

Bootstrap stdlib layout and the first app-host foundation skeleton exist. Full native
app-host behavior is still planned / not implemented yet.

## Purpose

Provide the developer-facing app host model: builder, app freeze, config, logging,
services, routes, validation, and modules.

## Scope

App builder, frozen graph, services, config, logging, route metadata, validation shape, and
ergonomic public API.

## Non-goals

No Node compatibility by default and no raw primitive-first public app model.

## Public/Internal API

`stdlib/sloppy/index.js` now re-exports frozen `Sloppy` and `Results` modules for the
future public `"sloppy"` facade. Implemented bootstrap behavior is intentionally small:

- `Results.text(...)` and `Results.json(...)` create frozen plain descriptors;
- `Sloppy.createBuilder()` creates a JavaScript bootstrap builder;
- `builder.config.addObject(...)` stores object-backed config values, with later object
  providers overriding earlier keys;
- `builder.logging.setMinimumLevel(...)` and `builder.logging.addMemorySink()` configure a
  deterministic memory logger with no timestamps;
- `builder.services.addSingleton(...)` and `builder.services.addTransient(...)` register
  string-token services;
- `builder.build()` freezes builder mutation and creates a frozen JavaScript app facade;
- `Sloppy.create()` remains supported as a default builder plus `build()`;
- `app.mapGet(pattern, handler)` stores an in-memory GET route registration;
- `.withName(name)` stores a route name;
- `app.freeze()` idempotently freezes route/endpoint mutation;
- `app.isFrozen()` reports app freeze state;
- route handlers invoked through snapshots receive a minimal `{ services, config, log }`
  context;
- `app.__getRoutes()` returns frozen route snapshots for bootstrap tests/debugging.

`examples/hello/app.js` demonstrates this current facade through a relative source import
from `stdlib/sloppy/index.js`. The example documents the future bare `"sloppy"` import and
`sloppy run` workflow as planned behavior only.

Native app graph validation, `app.run`, `app.listen`, `app.build`, compiler extraction,
automatic `app.plan.json` emission, HTTP server behavior, route groups, modules,
middleware, validation, config file/env providers, console/file/native logging sinks,
request-scoped service lifetimes, disposal hooks, async factories, and typed service tokens
remain future work.

## Ownership/Lifetime Rules

Current service lifetimes are JavaScript-only singleton and transient registrations.
Future real request scopes and service lifetimes must be explicit and plan-visible.

## Invariants

The current bootstrap freeze is structural only. The future native app graph freezes before
run in static plan mode.

## Diagnostics

Implemented bootstrap errors are thrown JavaScript `Error`/`TypeError` values for invalid
config keys, invalid log levels, duplicate/missing service tokens, invalid routes, and
mutation after freeze. Native diagnostics for missing service, duplicate route, invalid
lifetime, missing config, and module graph errors remain future work.

## Tests

CTest registers `bootstrap.stdlib.assets` to verify the source bootstrap files and copied
build-tree assets exist. CTest also registers `bootstrap.stdlib.api_shape` to statically
check the implemented bootstrap API names, descriptor fields, route registration shape, and
absence of future app-host APIs. When `node` is available, CTest also registers
`bootstrap.stdlib.app_host_foundation` to execute the ESM stdlib and cover builder freeze,
config, logging, services, route context, and app freeze behavior. V8-backed ESM stdlib
tests, plan fixtures, diagnostics snapshots, and full integration smoke remain future work
once module loading exists in the V8 bridge.
CTest also registers `examples.hello.api_shape` to statically check that the hello example
files exist, use the current stdlib import path, use `Sloppy.createBuilder`, `builder.build`,
`app.mapGet`, `Results.text`, and avoid package-manager scope.

## Source Docs

- `docs/developer-ergonomics.md`;
- `docs/modularity.md`;
- `docs/app-plan.md`;
- `docs/testing-strategy.md`;
- `docs/build-and-distribution.md`;
- `stdlib/sloppy/README.md`.

## Open Questions

- Exact package layout for the public TypeScript API.
