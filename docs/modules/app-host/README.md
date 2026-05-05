# App Host Module

## Status

Bootstrap stdlib layout and the first app-host foundation skeleton exist. MAIN1-03 adds a
small native app-host hardening layer for the supported artifact runtime path:
`sl_app_host_validate_startup` validates parsed Plan metadata before V8/user execution, and
`SlAppRequestScope` gives native request dispatch a deterministic cleanup boundary.
ENGINE-07 adds `SlAppLifecycle`, an explicit app startup/shutdown cleanup scope used by
`sloppy run` to release app-scoped resources such as the engine. ENGINE-16.A/B expands
that native lifecycle into explicit created, starting, running, stopping, draining,
stopped, and failed states, plus app/request identity fields for native diagnostics and
tests.
Full native app-host behavior is still not implemented.

Post-Core framework/API shape is locked in
`docs/project/framework-api-shape.md`. The current bootstrap app-host still exposes
`mapGet`, route groups, builder config, service registration, and module debug metadata;
the next framework target is Minimal API `app.get/post/...`, function modules first,
layered Plan-visible config, explicit provider imports, inferred provider capabilities,
explicit `ctx` binding helpers, and explicit `Results.*` descriptors.
FRAMEWORK-01.B implements the first typed config surface in the stdlib: case-insensitive
logical keys, nested `addObject`, `getString/getInt/getNumber/getBool`, `bind`, and
provider shorthand binding such as `sqlite:main`.

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
- `builder.config.addObject(...)` stores flat or nested object-backed config values, with
  later object providers overriding earlier keys;
- `builder.config` and `app.config` expose typed getters plus `bind(prefix, schema)`;
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
- `data.sqlite` exposes SQLite provider metadata and an `open(options)` entry point that
    works only in V8-enabled contexts with the SQLite bridge installed;
- `Sloppy.create()` remains supported as a default builder plus `build()`;
- `app.use(sqlite("main"))` accepts a provider descriptor, binds
  `Sloppy:Providers:sqlite:main`, and lets inline provider options override config
  defaults in the bootstrap facade;
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
compiled to artifacts that the EPIC-22/24 dev-only `sloppy run --artifacts` path can load
when V8 is enabled. EPIC-24 does not make V8 load the public ESM stdlib directly; generated
artifacts still run as classic scripts after `sloppy run` loads
`stdlib/sloppy/internal/runtime-classic.js` from the staged bootstrap stdlib root.

`sloppy run --artifacts` now starts a native app lifecycle, performs native app graph
startup validation after plan and artifact metadata load and before route materialization,
V8 creation, bootstrap evaluation, or request serving, then shuts that lifecycle down on
command exit or startup failure. Startup transitions through `STARTING` to `RUNNING`;
startup failure can close already-registered app cleanups exactly once and mark the
lifecycle `FAILED`. The supported startup checks cover Plan v1 compatibility,
supported target/runtime values, handler table presence and duplicate IDs, runnable GET
route metadata, route-to-handler references, duplicate method/pattern pairs, duplicate
non-empty route names, provider/capability token consistency, and duplicate provider
service tokens when `dataProviders[].service` is represented.
`sloppy run <source.js>` and `sloppy run` with `sloppy.json` compile first through
`sloppyc` and then enter this same artifact-backed app-host path. They do not make the
native app host discover routes or providers from source at runtime.

FRAMEWORK-01.F adds executable/source-input examples for the current app-host path:
`examples/hello-minimal`, `examples/users-api-sqlite`, `examples/configured-api`,
`examples/modules-api`, and `examples/validation-errors`. They are still compiler-owned
source inputs that become classic artifacts before the app host starts; the native app host
does not inspect source files at runtime.

MAIN1-10 adds a native capability registry that future provider bridge calls can receive
from the parsed plan. The registry is immutable after startup and has no global mutable
state. It can deny database access before provider work when a boundary supplies token,
operation, and provider metadata; filesystem/network checks are skeleton metadata checks
only and do not add filesystem or network APIs.

`app.run`, `app.listen`, `app.build`, automatic `app.plan.json` emission from the
bootstrap facade, real data providers, database connections from JavaScript, SQL execution
from JavaScript, nested route groups, module package loading, native plugins, middleware,
automatic validation/request binding, config file/env providers, console/file/native
logging sinks, async request lifetimes, async service factories, and typed service tokens
remain future work. MAIN1-02 validates compiler-emitted route/provider/capability plan
metadata, and MAIN1-03 validates that metadata at app-host startup, but neither PR makes
the bootstrap app host emit plans, activates services, opens providers, implements DI, or
enforces provider/capability access.

Controllers, decorators, constructor injection, full DI, implicit object-to-JSON responses,
and public-alpha example docs remain deferred until the corresponding issues land.

## Ownership/Lifetime Rules

Current service lifetimes are JavaScript-only singleton and transient registrations.
Capability metadata is copied and frozen for debug/introspection. Native request execution
now begins with a request scope and closes that scope after handler success, sync failure,
bounded async resolve/reject/pending failure, cancellation/deadline-style statuses, and
unsupported pre-handler outcomes. Request scopes may be tied to a running app lifecycle,
carry app/request IDs, increment the app active-request count, and decrement it on close.
Beginning shutdown stops new request scopes; if request scopes are still active, the app
enters `DRAINING` until callers close them or explicitly force shutdown. Forced shutdown
closes app-scope cleanups exactly once and does not make a production graceful-drain claim.
Request-scope cleanup uses `SlScope` LIFO order and owns cleanup registrations only;
cleanup payloads remain caller-owned. Request-scoped native resources are closed through
caller-owned `SlAppResourceCleanup` payloads that call `SlResourceTable` by `SlResourceId`.
ENGINE-16.C adds explicit terminal request outcomes before cleanup runs, covering success,
sync error, V8 exception, Promise rejection, validation/body parse failure, timeout,
cancel, client disconnect, response write failure, provider failure, provider pre-start
cancel, shutdown, and backpressure. A late completion after the scope is terminal is
rejected as stale lifecycle work and does not re-run cleanup. Bare request-scope close
requires a pre-recorded terminal outcome, except for the forced-shutdown cleanup case where
the app lifecycle supplies a shutdown terminal reason. Typed resource cleanup can
require an expected `SlResourceKind` through `SlAppTypedResourceCleanup`; wrong-kind
cleanup preserves the live resource and its ID instead of closing an unexpected handle.
`SlAppLifecycle` applies the same cleanup model to app-scoped resources and makes shutdown
idempotent. No raw native pointer is exposed to JavaScript.

Real service lifetimes, service disposal, async scope retention, and capability enforcement
must be explicit and plan-visible before public app-host services can claim runtime
lifetime semantics.

## Invariants

The current bootstrap freeze is structural only. The native startup validator freezes only
the supported Plan-backed app graph for the dev runtime; it does not execute module phases
or allow dynamic graph mutation. The future full native app graph freezes before run in
static plan mode.

## Diagnostics

Implemented bootstrap errors are thrown JavaScript `Error`/`TypeError` values for invalid
config keys, invalid log levels, duplicate/missing service tokens, invalid capability
tokens, duplicate/missing capabilities, invalid database capability metadata, invalid query
template usage, fake data provider missing methods, transaction misuse, invalid routes,
invalid route groups, invalid result status/header options, invalid schemas, duplicate
module names, invalid module objects, missing module dependencies, module dependency
cycles, phase callback failures, and mutation after freeze. Native startup diagnostics now
cover missing route handlers, duplicate routes, duplicate route names, invalid
provider/capability metadata, duplicate provider service tokens, and startup validation
failure summaries in `sloppy run`. `SLOPPY_E_APP_LIFECYCLE` covers native app lifecycle
state errors such as registering cleanup before startup, double start, request-scope
creation after shutdown starts, attempting to finish shutdown while requests are draining,
late request completion after terminal cleanup, and cleanup failure summaries. It renders
through the stable JSON diagnostic renderer.
Native diagnostics for a real module graph, missing service
activation, invalid lifetime dependencies, missing config providers, automatic request
validation, provider driver/config failures, and cleanup callback failure details remain
future work.

## Tests

CTest registers `core.app_host.hardening` to cover native app-host startup validation,
request-scope cleanup on handler success/failure/cancellation/deadline/unsupported
outcomes, app lifecycle start/double-start/startup-failure cleanup, graceful draining,
forced shutdown, app/request identity propagation, request-scope access after close,
app-scope versus request-scope resource ownership, terminal-outcome cleanup across
success/error/cancel/timeout/disconnect/provider/write/shutdown/backpressure cases, stale
late-completion rejection, typed resource cleanup wrong-kind preservation, app shutdown
cleanup of app-scoped resource IDs, idempotent shutdown, and stable lifecycle diagnostic
JSON. CTest registers
`bootstrap.stdlib.assets` to verify the source bootstrap files and copied
build-tree assets exist. CTest also registers `bootstrap.stdlib.api_shape` to statically
check the implemented bootstrap API names, descriptor fields, route registration/group
shape, schema export, module API shape, and absence of future app-host APIs. When `node` is available, CTest
also registers `bootstrap.stdlib.app_host_foundation` to execute the ESM stdlib and cover
builder freeze, config, logging, services, route groups, result helpers, schema validation,
route context, and app freeze behavior. V8-backed classic bootstrap runtime tests cover
the generated artifact path, but true V8 ESM stdlib tests, plan fixtures, diagnostics
snapshots, and full app-host integration smoke remain future work once ESM loading exists
in the V8 bridge.
`bootstrap.stdlib.modules` executes the ESM stdlib with Node when available and covers
module API shape, builder integration, dependency ordering, missing dependency and cycle
errors, duplicate module names, phase failure context, route/service attribution, and
module debug metadata.
`bootstrap.stdlib.data_foundation` executes the ESM stdlib with Node when available and
covers database capability metadata, query template lowering, fake data provider methods,
transaction commit/rollback behavior, nested transaction rejection, use after close, and
module/service integration, plus the `data.sqlite` bridge-unavailable and mocked-bridge
stdlib paths. Native SQLite execution itself is covered by the `data.sqlite.provider` CTest
target, and MAIN1-08 adds separate V8-gated SQLite bridge coverage.
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
