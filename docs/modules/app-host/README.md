# App Host Module

## Status

Bootstrap stdlib layout and the first app-host foundation skeleton exist. Full native
app-host behavior is still planned / not implemented yet.

## Purpose

Provide the developer-facing app host model: builder, app freeze, config, logging,
services, routes, route groups, validation shape, and modules.

## Scope

App builder, frozen graph, services, config, logging, route metadata, validation shape, and
ergonomic public API.

## Non-goals

No Node compatibility by default and no raw primitive-first public app model.

## Public/Internal API

`stdlib/sloppy/index.js` now re-exports frozen `Sloppy`, `data`, `sql`, `Results`, and `schema` modules for the
future public `"sloppy"` facade. Implemented bootstrap behavior is intentionally small:

- `Results.text(...)`, `Results.json(...)`, and the EPIC-13 status/problem helper set
  create frozen plain descriptors;
- `schema.string()`, `schema.number()`, `schema.boolean()`, and `schema.object(...)`
  create inspectable bootstrap validation skeletons with standalone `validate(...)`;
- `Sloppy.createBuilder()` creates a JavaScript bootstrap builder;
- `builder.config.addObject(...)` stores object-backed config values, with later object
  providers overriding earlier keys;
- `builder.logging.setMinimumLevel(...)` and `builder.logging.addMemorySink()` configure a
  deterministic memory logger with no timestamps;
- `builder.capabilities.addDatabase(...)` registers database capability metadata;
- `builder.services.addSingleton(...)` and `builder.services.addTransient(...)` register
  string-token services;
- `Sloppy.module(name)` creates a bootstrap app module definition with dependencies,
  capabilities, services, routes, and simple metadata;
- `builder.addModule(module)` registers a module definition, freezes further module
  mutation, validates duplicate module names, and participates in `builder.build()`;
- `builder.build()` freezes builder mutation and creates a frozen JavaScript app facade;
- `builder.build()` resolves module dependencies, detects missing dependencies and cycles,
  runs module capabilities before services before routes in dependency order, and attaches
  module debug metadata;
- `sql` lowers tagged SQL templates to query descriptors without interpolating values into
  SQL text;
- `data.createFakeProvider(...)` exposes the JS-only fake provider contract for
  tests/examples;
- `data.sqlite` exposes SQLite provider metadata and a future `open(options)` entry point
  that fails honestly until native stdlib intrinsics exist;
- `Sloppy.create()` remains supported as a default builder plus `build()`;
- `app.mapGet(pattern, handler)` stores an in-memory GET route registration;
- `app.mapGet(pattern, metadata, handler)` stores route metadata such as validation
  schemas without executing validation;
- `app.mapGroup(prefix)` creates an in-memory route group with prefix composition,
  `.withTags(...)`, `.withName(...)`, and grouped `.mapGet(...)`;
- `.withName(name)` stores a route name;
- `app.freeze()` idempotently freezes route/endpoint mutation;
- `app.isFrozen()` reports app freeze state;
- route handlers invoked through snapshots receive a minimal `{ services, config, log }`
  context;
- service factory scopes expose `scope.capabilities` for bootstrap capability metadata
  lookup;
- `app.capabilities.has/get/list` exposes frozen capability metadata;
- `app.__getRoutes()` returns frozen route snapshots for bootstrap tests/debugging.
- `app.__debug().modules`, `app.__getModuleGraph()`, and
  `app.__getPlanContributions().modules` return bootstrap-only module debug metadata.

`examples/hello/app.js` demonstrates this current facade through a relative source import
from `stdlib/sloppy/index.js`. That example is still not a `sloppy run` artifact app. The
compiler-owned `examples/compiler-hello/` input uses the bare `"sloppy"` import and can be
compiled to artifacts that the EPIC-22 dev-only `sloppy run --artifacts` path can load when
V8 is enabled.

Native app graph validation, `app.run`, `app.listen`, `app.build`, automatic
`app.plan.json` emission from the bootstrap facade, real data providers, database
connections from JavaScript, SQL execution from JavaScript, nested route groups, module
package loading, native plugins,
middleware, automatic validation/request binding, config file/env providers, console/file/native
logging sinks, request-scoped service lifetimes, disposal hooks, async factories, and typed
service tokens remain future work.

## Ownership/Lifetime Rules

Current service lifetimes are JavaScript-only singleton and transient registrations.
Capability metadata is copied and frozen for debug/introspection. Future real request
scopes, service lifetimes, and capability enforcement must be explicit and plan-visible.

## Invariants

The current bootstrap freeze is structural only. The future native app graph freezes before
run in static plan mode.

## Diagnostics

Implemented bootstrap errors are thrown JavaScript `Error`/`TypeError` values for invalid
config keys, invalid log levels, duplicate/missing service tokens, invalid capability
tokens, duplicate/missing capabilities, invalid database capability metadata, invalid query
template usage, fake data provider missing methods, transaction misuse, invalid routes,
invalid route groups, invalid result status/header options, invalid schemas, duplicate
module names, invalid module objects, missing module dependencies, module dependency
cycles, phase callback failures, and mutation after freeze. Native diagnostics for missing
service, duplicate route, invalid lifetime, missing config, validation failure, provider
driver/config failures, and module graph errors remain future work.

## Tests

CTest registers `bootstrap.stdlib.assets` to verify the source bootstrap files and copied
build-tree assets exist. CTest also registers `bootstrap.stdlib.api_shape` to statically
check the implemented bootstrap API names, descriptor fields, route registration/group
shape, schema export, module API shape, and absence of future app-host APIs. When `node` is available, CTest
also registers `bootstrap.stdlib.app_host_foundation` to execute the ESM stdlib and cover
builder freeze, config, logging, services, route groups, result helpers, schema validation,
route context, and app freeze behavior. V8-backed ESM stdlib tests, plan fixtures,
diagnostics snapshots, and full integration smoke remain future work once module loading
exists in the V8 bridge.
`bootstrap.stdlib.modules` executes the ESM stdlib with Node when available and covers
module API shape, builder integration, dependency ordering, missing dependency and cycle
errors, duplicate module names, phase failure context, route/service attribution, and
module debug metadata.
`bootstrap.stdlib.data_foundation` executes the ESM stdlib with Node when available and
covers database capability metadata, query template lowering, fake data provider methods,
transaction commit/rollback behavior, nested transaction rejection, use after close, and
module/service integration, plus the `data.sqlite` bridge-unavailable stdlib entry point.
Native SQLite execution itself is covered by the `data.sqlite.provider` CTest target.
CTest also registers `examples.hello.api_shape` to statically check that the hello example
files exist, use the current stdlib import path, use `Sloppy.createBuilder`, `builder.build`,
`app.mapGet`, `Results.text`, and avoid package-manager scope.
`examples.ergonomics.api_shape` statically checks the EPIC-13 example for route groups,
result helpers, schema metadata, and honest non-runnable status text.
`examples.modules_basic.api_shape` statically checks the EPIC-14 module example for module
dependencies, service/route contributions, builder registration, and honest non-runnable
status text.
`examples.data_foundation.api_shape` statically checks the EPIC-15 data/capabilities
example for capability declaration, fake data service registration, query template usage,
transaction skeleton usage, and honest non-runnable/no-real-provider status text.

## Source Docs

- `docs/developer-ergonomics.md`;
- `docs/modularity.md`;
- `docs/app-plan.md`;
- `docs/testing-strategy.md`;
- `docs/build-and-distribution.md`;
- `stdlib/sloppy/README.md`.

## Open Questions

- Exact package layout for the public TypeScript API.
