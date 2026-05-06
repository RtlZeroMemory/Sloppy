# Developer Ergonomics

## Purpose

Developer ergonomics is Sloppy's primary product wedge. Sloppy should make TypeScript
backend development feel designed, not assembled from runtime primitives, framework soup,
and npm debris.

The name is silly. The user experience should be sharp. A Sloppy application should read
like an application host: configuration, logging, services, modules, routes, validation,
results, diagnostics, and permissions are first-class concepts.

## Scope

This specification covers:

- public app API shape;
- app-host model;
- builder/app/module growth path;
- route groups;
- `Results.*`;
- validation as route shape;
- built-in config, logging, services, data providers, and capabilities;
- diagnostics as product UX;
- Sloppy Plan-powered CLI tooling;
- implementation epics and acceptance criteria.

## Non-Goals

Sloppy ergonomics are not:

- Node, Bun, or Deno compatibility first;
- an Express clone;
- a raw `fetch(req)` callback as the primary model;
- ORM-first;
- hiding all runtime behavior behind magic;
- sacrificing explicit diagnostics for clever inference.

## Current Phase

The repository contains metadata-only CLI introspection commands and a source-controlled
bootstrap stdlib layout.
TASK 11.B/11.C added the first tiny public JavaScript facade inside that stdlib:
`Results.text(...)`, `Results.json(...)`, `Sloppy.create()`, and `app.mapGet(...)`.
TASK 12.A/12.B/12.C/12.D extends that facade with the first app-host foundation skeleton:
`Sloppy.createBuilder()`, `builder.build()`, structural `app.freeze()`, object-backed
config, deterministic memory logging, and string-token singleton/transient services.
TASK 13.A/13.B/13.C/13.D extends the same bootstrap facade with in-memory route groups,
the fuller bounded `Results.*` helper set, a small `schema` validation skeleton, and an
`examples/ergonomics/` API-shape fixture.
TASK 14.A/14.B/14.C/14.D adds the same kind of bootstrap-only module skeleton:
`Sloppy.module(...)`, `builder.addModule(...)`, dependency graph ordering, services/routes
phase execution, module attribution, module debug metadata, and module graph errors.
TASK 15.A/15.B/15.C/15.D adds database capability metadata, the module capabilities phase,
query template lowering, the common fake-provider data API shape, and transaction callback
semantics for tests/examples.
EPIC-16 adds native SQLite provider execution in C tests and exposes `data.sqlite` as the
future stdlib entry point; JavaScript-to-native database calls are still deferred until
runtime intrinsics exist.
EPIC-19 adds `sloppy routes`, `sloppy doctor`, `sloppy audit`, and `sloppy openapi` over
plan-compatible metadata fixtures/artifacts. These commands do not compile apps, run
handlers, start HTTP, enter V8, or run live provider checks by default.
EPIC-21 adds the first compiler extraction path, and ENGINE-02 expands it into the
supported compiler/Plan pipeline. `sloppyc build` can parse one supported public API source
file with the bare `"sloppy"` import, extract literal GET/POST/PUT/PATCH/DELETE route
metadata, simple route groups, direct async handler metadata, and minimal SQLite
capability/provider metadata, assign stable handler IDs, and emit deterministic
`app.plan.json`, `app.js`, and real handler-line `app.js.map` artifacts.
EPIC-24 makes that bare `"sloppy"` import an explicit compiler rewrite story rather than a
Node resolution promise. The compiler accepts only `"sloppy"`, emits generated code that
reads bootstrap runtime state from `globalThis.__sloppy_runtime`, and rejects arbitrary
bare imports such as `"express"`, `"fs"`, and `"node:fs"`.
CORE-FS-01.A/B locks the first filesystem API contract around `sloppy/fs`. The intended
developer shape is async and direct, for example `await File.readText("./data/users.json")`,
while serious apps move to named roots such as `data:/users.json`. This slice makes the
feature Plan-visible as `stdlib.fs`; CORE-FS-01.C through CORE-FS-01.G implement the
native/JavaScript bridge for core file, directory, handle, stream, and bounded watch
helpers, while CORE-FS-01.I/J adds source examples and CLI metadata evidence.
CORE-TIME-01.A/B locks the `sloppy/time` contract around `Time`, `Deadline`,
`CancellationController`, async delay/timeout, intervals, interval-based scheduled jobs,
and explicit fake clocks. The compiler now makes `sloppy/time` Plan-visible as
`stdlib.time`. CORE-TIME-01.C/D/G adds the V8-backed `Time.delay`, `Time.timeout`,
`Deadline`, `CancellationController`, and `Time.yield` runtime path. CORE-TIME-01.E/F
adds async iterable intervals, interval-based scheduled jobs, `Time.systemClock`, and
explicit test-scoped `Time.fakeClock`. CORE-TIME-01.H lets filesystem calls accept
`signal`, `deadline`, and `timeoutMs` without claiming Node timer compatibility or
preemptive native filesystem interruption. CORE-TIME-01.I adds focused examples for
delay/timeout, deadline/cancellation, intervals/scheduled jobs, fake clocks, and filesystem
deadline integration.
CORE-CRYPTO-01.C/D/F/H implements the first `sloppy/crypto` runtime surface around
`Random`, `Hash`, `Hmac`, `ConstantTime`, and `Secret`. The compiler makes
`sloppy/crypto` Plan-visible as `stdlib.crypto`, and active V8 plans receive the private
`__sloppy.crypto` bridge. `Password` and `NonCryptoHash` remain visible but fail closed
until their dedicated slices land. The API is Sloppy-shaped and does not promise
WebCrypto, Node crypto, or Bun compatibility.
CORE-NET-01.A/B locks the first `sloppy/net` TCP contract around `TcpClient`,
`TcpListener`, `TcpConnection`, and `NetworkAddress`. The compiler now makes `sloppy/net`
Plan-visible as `stdlib.net`, but the runtime feature remains unavailable by default until
native TCP/libuv backends and the V8 bridge land. Development-mode loopback should stay
easy, while strict mode remains able to require explicit external connect/listen policy.
This facade is still in-memory and conceptual only. It does not run an app, emit a Sloppy
Plan by itself, serve HTTP, validate requests, load module packages, or integrate native
modules or call real database providers from JavaScript.
`examples/hello/` demonstrates the checked-in bootstrap API shape through a relative import
from `stdlib/sloppy/index.js`. `examples/compiler-hello/` demonstrates the runnable
compiler path with the public bare `"sloppy"` specifier; running that artifact still
requires a V8-enabled build and the staged bootstrap stdlib assets.

## ENGINE-01 Contract Lock

`docs/project/engine-framework-contract.md` locks the foundation contract before the next
implementation layers broaden the compiler, V8 runtime, HTTP runtime, and SQLite bridge.

Foundation ergonomics are intentionally small:

- `Sloppy.create()` plus default-exported app is the core app handoff.
- `app.mapGet`, `app.mapPost`, `app.mapPut`, `app.mapPatch`, and `app.mapDelete` are the
  supported HTTP route declarations for the foundation.
- `Results.*` descriptors are the handler response contract.
- a handler context exposes `route`, `query`, `request`, `signal`, `deadline`, and future
  request-owned `resources`.
- async handlers returning Promises are required; V8 microtask draining, request-scope
  retention, cancellation, deadlines, and bounded queues are part of the foundation.
- JSON/text bodies, headers, route params, query params, result serialization, and safe
  error responses are core HTTP runtime work.
- SQLite is the core data provider foundation with `data.sqlite.open`, `exec`, `query`,
  `queryOne`, `transaction`, and `close`.

The foundation workflow can still be driven explicitly through artifacts:

```powershell
sloppyc build app.js --out .sloppy
sloppy run --artifacts .sloppy --host 127.0.0.1 --port 5173
```

ENGINE-02.E adds `sloppy run app.js` and `sloppy run` with `sloppy.json` as shortcuts over
that same compiler/artifact/runtime path. Source-input run rebuilds through `sloppyc`,
validates artifacts, and does not add Node/npm compatibility, package-manager behavior,
watch mode, hot reload, public alpha docs, or benchmark claims. `app.run`, broad
service/module registration, and PostgreSQL/SQL Server JavaScript bridges remain deferred.

## Future Phase

The first ergonomic implementation should prove the tiny app shape with a handwritten or
fake-emitted plan before adding broad API surface. Later phases grow from the same app-host
model rather than switching frameworks.

## Product Wedge

Performance matters, but final Sloppy primarily competes on backend app ergonomics.

Node, Bun, and Deno expose useful runtime primitives. Sloppy should expose an app host. The
normal path should be:

1. declare app structure;
2. let `sloppyc` extract the Sloppy Plan;
3. let the native host validate graph, services, permissions, and routes at startup;
4. run handlers through stable handler IDs with good diagnostics.

## Tiny App Shape

The smallest app should not require ceremony:

```ts
import { Sloppy, Results } from "sloppy";

const app = Sloppy.create();

app.mapGet("/", () => Results.text("Sloppy is alive"));

export default app;
```

Acceptance criteria for this API shape:

- one import gives the core app API and results helpers;
- `Sloppy.create()` is equivalent to a default builder plus `build()`;
- route declaration remains compiler-readable;
- handler receives a typed context only when it asks for one;
- emitted plan contains one route and one handler ID;
- runtime startup uses the Plan-backed artifact path; `sloppy run <source>` is only a
  compiler handoff into that path, and `app.run` remains deferred.

TASK 11.A creates the bootstrap source layout that will eventually provide the `"sloppy"`
module:

```text
stdlib/sloppy/
  index.js
  app.js
  results.js
  internal/intrinsics.js
```

TASK 11.B/11.C turns the first two placeholders into a minimal implemented API shape:
`Results.text`, `Results.json`, `Sloppy.create`, `app.mapGet`, `.withName`, and
`app.__getRoutes()` for tests/debugging. TASK 12.A/12.B/12.C/12.D adds a bootstrap
builder/app host skeleton. `Sloppy.create()` is now equivalent to a default bootstrap
builder plus `build()`, but this is still a JavaScript-only structural facade rather than a
native app graph. TASK 13 adds route groups, route metadata storage, `schema`, and the
bounded result helper set to that same JavaScript-only facade.
TASK 14 adds `Sloppy.module(...)`, `builder.addModule(...)`, dependency ordering, module
services/routes phases, and bootstrap module debug metadata.

TASK 11.D adds the first checked-in tiny app example:

```text
examples/hello/
  README.md
  app.js
```

That example is static bootstrap validation only. It is not compiled by `sloppyc`, does not
emit `app.plan.json`, and does not serve HTTP.

EPIC-21 adds `examples/compiler-hello/` as the compiler input example. It uses
`import { Sloppy, Results } from "sloppy";`, can be compiled with `sloppyc build`, and can
now be run through the direct source-input shortcut or the explicit V8-required dev-only
`sloppy run --artifacts` path. EPIC-24 runtime startup loads the classic bootstrap runtime
asset before the generated app artifact, then dispatches through
`__sloppy_register_handler` registrations.

FRAMEWORK-01.F adds the hardened current example set:

- `examples/hello-minimal/` for `sloppy run`, `sloppy.json`, route binding, and Results.
- `examples/users-api-sqlite/` for config-driven SQLite, inferred/generated capabilities,
  module routes, and V8-gated transport evidence.
- `examples/configured-api/` for `appsettings.json`, environment overlay, and config reads.
- `examples/modules-api/` for relative function modules and route groups.
- `examples/validation-errors/` for schema-backed body metadata and OpenAPI validation
  problem shape.

The examples are intentionally honest: V8 execution is marked where required, schema
metadata does not claim runtime semantic validation, and no example introduces Node/npm,
package-manager behavior, public alpha promises, benchmark claims, or production HTTP
claims.

## Builder/App Shape

Structured apps use the builder:

```ts
const builder = Sloppy.createBuilder();

builder.config.addObject({
  "app.name": "hello",
});

builder.logging
  .setMinimumLevel("info")
  .addMemorySink();

builder.services.addSingleton("message", () => "Hello from Sloppy");

const app = builder.build();

app.mapGet("/", ({ services }) => Results.text(services.get("message")));
app.freeze();
```

The builder owns pre-run composition. The app owns the frozen graph after `build()`.

Implementation notes:

- builder calls must be extractable into Sloppy Plan metadata where possible;
- config keys may appear in the plan, but secret values must not;
- current bootstrap `build()` freezes builder-side config/logging/services mutation;
- current bootstrap `app.freeze()` freezes route registration and endpoint naming;
- current native app-host startup validation rejects malformed Plan-backed routes,
  route-handler mismatches, duplicate routes/names, and represented provider service-token
  conflicts before V8/user execution;
- future native app-host work will make build/freeze part of full module/service startup
  validation and plan emission;
- errors detected before `run()` should be startup diagnostics, not request-time surprises.

## Application Configuration

FRAMEWORK-01.B introduces the first Slop-owned configuration model. It is convention-first,
layered, typed, provider-aware, and visible in compiler-emitted Plan metadata. It is not a
Node/npm/package-manager system and does not expose `process.env` compatibility.

Configuration source precedence is:

1. built-in defaults;
2. `appsettings.json`;
3. `appsettings.{Environment}.json`;
4. canonical Sloppy environment variables;
5. selected CLI overrides;
6. explicit runtime/test overrides only where a harness already owns them.

`sloppy.json` is project/run configuration. It may choose `entry`, `outDir`, and
`environment` for the source-input dev loop. `appsettings*.json` is application
configuration. The selected environment overlays `appsettings.json` with
`appsettings.{Environment}.json`; `sloppy run --environment Development` overrides the
environment from `sloppy.json` for the compiler handoff. `--artifacts <dir>` remains an
explicit/debuggable artifact path and does not require source-input config.

Logical keys use colon separators and are looked up case-insensitively:

```text
Sloppy:Server:Host
Sloppy:Server:Port
Sloppy:Server:MaxConnections
Sloppy:Server:MaxRequestBodyBytes
Sloppy:Server:RequestTimeoutMs
Sloppy:Runtime:V8MicrotaskDrainLimit
Sloppy:Providers:sqlite:main:database
Sloppy:Providers:sqlite:main:queueCapacity
```

Missing optional `appsettings*.json` files are allowed. Malformed JSON, invalid type
conversion, missing required provider configuration, and invalid CLI/env overrides fail
with diagnostics. When keys look like secrets, passwords, tokens, API keys, or connection
strings, diagnostics and Plan metadata redact values.

Canonical environment variables are uppercase and use `SLOPPY_` plus `__` separators, for
example:

```powershell
$env:SLOPPY_SLOPPY__SERVER__PORT = "5173"
$env:SLOPPY_SLOPPY__PROVIDERS__SQLITE__MAIN__DATABASE = ".\app.db"
```

The currently supported CLI overrides are bounded: `--environment`, `--host`, and
`--port`. They override JSON and environment variables for the compiler-visible
configuration model. Broad arbitrary `--config:*` CLI binding is deferred.

JavaScript config access is typed:

```ts
const port = app.config.getInt("Sloppy:Server:Port", 5173);
const host = app.config.getString("Sloppy:Server:Host", "127.0.0.1");
const enabled = app.config.getBool("Feature:X", false);
const limit = app.config.getNumber("Feature:Limit", 1.5);
const options = app.config.bind("sqlite:main", SqliteOptions);
```

`getString`, `getInt`, `getNumber`, and `getBool` return defaults when supplied. Without a
default, a missing key is required and fails clearly. Invalid conversions fail clearly.
`bind(prefix)` returns a plain object for the subtree under `prefix`; provider shorthand
such as `sqlite:main` maps to `Sloppy:Providers:sqlite:main`. When a schema/constructor is
supplied, Sloppy passes the bound object into that schema.

## Route Groups

Route groups are the normal scaling path for related endpoints:

```ts
const users = app
  .mapGroup("/users")
  .withTags("Users");

users.mapGet("/{id:int}", getUser)
  .withName("Users.Get");

users.mapPost("/", createUser)
  .withName("Users.Create");
```

Native TASK 10.A route pattern support is intentionally smaller than the final public API
shown above. The implemented C parser/matcher supports only `/`, static segments,
`{name}`, `{name:str}`, and `{name:int}`. It does not implement route groups, method
matching, route precedence, percent decoding, query string matching, HTTP serving,
middleware, validation, or OpenAPI metadata yet. TASK 11.C's JavaScript `app.mapGet`
stores conceptual in-memory registrations only and is not connected to that native route
foundation or `app.plan.json` emission yet.

Route groups contribute:

- path prefix;
- tags/metadata;
- middleware;
- endpoint filters;
- result filters;
- service requirements;
- permission requirements;
- source locations for diagnostics.

Current TASK 13.A bootstrap behavior:

- `app.mapGroup(prefix)` returns an in-memory route group.
- `prefix` must start with `/`; trailing slashes are normalized except for root `/`.
- Child patterns may be relative or start with `/`.
- `"/users"` plus `"{id:int}"` becomes `"/users/{id:int}"`; `"/users/"` plus
  `"/active"` becomes `"/users/active"`.
- `.withTags(...tags)` and `.withName(name)` store group metadata on child route snapshots.
- Grouped `.mapGet(...)` registers ordinary in-memory GET routes and fails after
  `app.freeze()`.
- Nested groups, middleware/filter contribution, duplicate route diagnostics, compiler
  extraction, app-plan emission, and native HTTP route table integration remain future work.

## Results Model

Handlers return values or result helpers. They do not mutate raw response objects by
default.

Planned helpers:

- `Results.ok(value)`;
- `Results.created(location, value)`;
- `Results.accepted(location?, value?)`;
- `Results.noContent()`;
- `Results.notFound(value?)`;
- `Results.badRequest(value?)`;
- `Results.problem(problemDetails)`;
- `Results.text(text, options?)`;
- `Results.html(html, options?)`;
- `Results.json(value, options?)`;
- `Results.file(fileCapability, options?)`;
- `Results.stream(streamResource, options?)`.

Internal architecture expectation:

- JS result helpers produce a compact result descriptor;
- V8 bridge converts descriptors into native response results;
- native fast paths handle text, JSON, no-content, and common status responses;
- unsupported result shapes produce diagnostics with handler name and source location.

Current TASK 13.B descriptor helpers:

- `Results.ok`, `Results.created`, `Results.accepted`, `Results.noContent`,
  `Results.notFound`, `Results.badRequest`, `Results.problem`, `Results.text`,
  `Results.json`, and `Results.html` return frozen plain descriptors with the
  `__sloppyResult: true` identity marker.
- `options.status` defaults to each helper's documented status and must be an integer from
  100 to 999.
- `options.headers` may be a plain object and is shallow-copied/frozen as descriptor
  metadata.
- JSON serialization, response writing, streaming, files, redirects, cookies, content
  negotiation, header normalization, and native descriptor conversion remain future work.

## Validation As Route Shape

Validation should be visible in the route declaration:

```ts
const CreateUser = object({
  name: string().min(1),
  email: string().email(),
});

app.mapPost(
  "/users",
  { body: CreateUser },
  async ({ body, services }) => {
    return Results.ok(body);
  },
);
```

Planned binding areas:

- `body`;
- `query`;
- `route`;
- `headers`.

Validation behavior:

- schema metadata is emitted into the Sloppy Plan;
- invalid input produces automatic `400` problem results later;
- diagnostics identify the route, schema, and failing field;
- schema metadata feeds future OpenAPI generation;
- validation code must not require raw request parsing in normal apps.

Current TASK 13.C bootstrap behavior:

- `schema.string()`, `schema.number()`, `schema.boolean()`, and `schema.object(shape)`
  return frozen schema objects.
- Each schema has `kind`, `metadata`, and `validate(value)`.
- `string.min(n)` and `string.email()` return new string schemas with inspectable rule
  metadata.
- Validation results are `{ ok: true, value }` or
  `{ ok: false, issues: [{ path, code, message }] }`.
- `app.mapGet(pattern, metadata, handler)` and grouped `.mapGet(...)` can store schema
  metadata such as `{ query: schema.object(...) }` on route snapshots.
- Request parsing, body/query binding, automatic `400` responses, OpenAPI generation,
  schema extraction into `app.plan.json`, async validation, coercion, arrays, unions, and
  custom refinements remain future work.

## Built-In App Host

Final built-ins:

- config;
- logging;
- services/DI;
- modules;
- middleware;
- endpoint filters;
- result filters;
- data providers;
- capabilities/permissions;
- diagnostics;
- app graph freeze.

These are app-host concepts, not optional framework packages. The implementation may keep
the v0.1 surface narrow, but the architecture should reserve these positions from the
start.

## Modules As Ergonomic Scaling Model

Tiny apps and large apps use the same model. A user should not rewrite from a toy runtime
primitive to a framework later.

```ts
builder
  .addModule(AuthModule)
  .addModule(UsersModule)
  .addModule(BillingModule);
```

Modules are explicit, not side-effect imports. They contribute services, routes,
middleware, filters, capabilities, permissions, health checks, jobs, and metadata to the
Sloppy Plan.

Current TASK 14 bootstrap behavior:

- `Sloppy.module(name)` creates a module definition.
- `.dependsOn(...)`, `.services(...)`, `.routes(...)`, and `.metadata(...)` are available.
- `builder.addModule(...)` registers modules and freezes the module definition.
- `builder.build()` validates missing dependencies and cycles.
- services run for all modules before routes, with each phase in deterministic dependency
  order.
- independent modules keep builder insertion order.
- module-created services and routes are attributed in `app.__debug().modules`.

The debug metadata is plan-like bootstrap introspection, not real `app.plan.json` emission.
Compiler extraction, package loading, native plugins, optional dependencies, versioning,
middleware, filters, capabilities, permissions, health checks, jobs, and data provider
modules remain future work.

## Data Provider Ergonomics

Database providers are modules.

Hard constraint: provider capabilities must not become routine TypeScript paperwork. The
runtime needs capability metadata for enforcement and diagnostics, but the framework and
compiler must generate the common provider capability entries from provider declarations
instead of requiring CRUD app authors to hand-write raw Plan `capabilities` blocks.
Hand-authored capability declarations are reserved for advanced cases such as split
read/write grants, multi-tenant/provider routing, plugin providers, or production
least-privilege reviews.

SQLite:

```ts
import { sqlite } from "sloppy:data/sqlite";

builder.addModule(
  sqlite.module({
    token: "data.main",
    database: "app.db",
  }),
);
```

PostgreSQL:

```ts
import { postgres } from "sloppy:data/postgres";

builder.addModule(
  postgres.module({
    token: "data.main",
    connectionString: builder.config.require("DATABASE_URL"),
  }),
);
```

SQL Server:

```ts
import { sqlserver } from "sloppy:data/sqlserver";

builder.addModule(
  sqlserver.module({
    token: "data.main",
    connectionString: builder.config.require("SQLSERVER_CONNECTION"),
  }),
);
```

Query templates parameterize by default:

```ts
const user = await db.queryOne`
  select id, name, email
  from users
  where id = ${route.id}
`;
```

Rules:

- no string-concat SQL as the blessed path;
- transactions are part of the common API;
- provider-specific APIs are allowed under provider namespaces;
- Sloppy does not pretend every SQL dialect is identical;
- diagnostics redact secrets and point to provider setup fixes.

Current TASK 15 bootstrap behavior:

- `builder.capabilities.addDatabase(...)` and module `.capabilities(...)` store database
  capability metadata.
- `sql` and `data.lowerQueryTemplate(...)` lower query templates to descriptors that keep
  text and parameters separate.
- placeholder styles supported today are `question`, `postgres`, and `named`.
- `data.createFakeProvider(...)` provides `query`, `queryOne`, `exec`, and `transaction`
  for tests/examples.
- transactions commit on resolved callbacks and roll back on thrown/rejected callbacks.
- nested transactions and use after closed transaction scopes fail.
- EPIC-16 native C tests prove SQLite `:memory:` open/close, exec/query/queryOne, primitive
  binding, transactions, and diagnostics.
- `data.sqlite.open(...)` opens a native resource only in V8-enabled contexts that install
  the SQLite bridge and pass the declared capability check; bootstrap-only and non-V8
  contexts report that the native stdlib bridge is unavailable.
- `data.sqlite.open({ access: "read" })` performs read-capability checks only; the default
  `readwrite` open path requires write capability.

Future framework/compiler requirement:

- provider modules such as `postgres.module({ token: "data.main", ... })` and
  `sqlite.module({ token: "data.main", ... })` must emit the default provider and
  capability Plan metadata automatically;
- `sloppyc` must preserve enough source/module attribution for `sloppy audit` and startup
  diagnostics to explain the generated capability;
- the default CRUD path should infer `readwrite` unless the provider/module declaration
  explicitly narrows it;
- advanced app code may still override or split capabilities explicitly, but examples and
  tutorials must prefer generated provider capabilities;
- direct low-level provider APIs that require manual capability tokens are escape hatches,
  not the blessed application authoring path.

Still not implemented: JavaScript-to-native PostgreSQL/SQL Server calls, production
pooling, migrations, native provider scheduling/async offload, filesystem/network
permission APIs, and compiler extraction of template literals. Native PostgreSQL and SQL
Server provider boundaries exist in C tests, with live execution gated by environment
variables.

## Diagnostics As Product UX

Good diagnostics are part of ergonomics. They are not polish after the fact.

Diagnostics must cover:

- missing service;
- duplicate route;
- missing config;
- database provider unavailable;
- permission denied;
- source-mapped handler exception.

Example:

```text
sloppy: service not registered

  Route:
    POST /users

  Handler:
    Users.Create

  Missing service:
    data.main

  Required by:
    users.repo

  Fix:
    builder.addModule(postgres.module({
      token: "data.main",
      connectionString: builder.config.require("DATABASE_URL")
    }))
```

Diagnostic acceptance criteria:

- stable machine-readable code exists;
- route and handler names are included where known;
- source locations map back to TypeScript where possible;
- fix text is concrete when safe;
- secret values are never printed.

## Sloppy Plan-Powered Tooling

CLI ergonomics should be powered by `app.plan.json`. The current native Plan parser still
knows only the minimal handler-oriented Plan v1 schema, so EPIC-19 reads documented interim
metadata sections from plan-compatible JSON files until compiler/app-host emission catches
up.

| Command | Purpose | Planned output | Plan sections |
| --- | --- | --- | --- |
| `sloppy routes` | Inspect route graph | methods, paths, handlers, source locations, request bindings, response metadata, completeness | `routes`, `handlers` |
| `sloppy capabilities` | Explain inferred route/provider authority | generated provider effects, access, reasons, source locations | `routes[].effects`, `capabilities`, `dataProviders` |
| `sloppy doctor` | Check environment, providers, and Plan completeness | missing drivers, invalid config, SDK issues, partial/runtime-only routes | `target`, `dataProviders`, `capabilities`, `routes`, `completeness` |
| `sloppy audit` | Explain static review findings | stable finding codes and CI-style errors/warnings | `capabilities`, `routes`, `modules`, `completeness` |
| `sloppy openapi` | Generate API description | supported OpenAPI subset plus Slop extensions | `routes`, `schemas`, `bindings`, `responses`, `effects` |
| `sloppy db migrate` | Future migration runner | migration plan/status | `dataProviders`, provider metadata |
| `sloppy db status` | Database health/config status | connection/provider status | `dataProviders`, `healthChecks` |
| `sloppy new api` | Create project skeleton | app scaffold | templates, not plan-dependent |
| `sloppy check` | Type and plan validation | diagnostics | compiler graph, plan metadata |
| `sloppy build` | Emit artifacts | `.sloppy/` output | all artifact sections |
| `sloppy run` | Build/cache/run | runtime startup diagnostics | all artifact sections |

Implemented EPIC-19/ENGINE-20.C command scope:

- `sloppy routes` prints method, pattern, handler ID, route name, module, source location,
  request bindings, response metadata, and completeness from Plan metadata;
- `sloppy capabilities` prints compiler-generated provider effects as inferred route
  capabilities, including provider kind, access, reason, and source location;
- `sloppy doctor` prints safe deterministic checks and redacts connection-string-like
  secrets, including Plan completeness and partial metadata checks;
- `sloppy audit` runs a small fixed metadata rule set for duplicate routes, missing
  handlers, module dependency problems, incomplete provider metadata, partial Strong Plan
  metadata, and skeleton capability caveats. ERROR findings return a nonzero process exit;
- `sloppy openapi` emits a Plan-derived OpenAPI 3.0.3 subset with route/query/header
  parameters, schema-backed JSON request bodies, known response statuses, Slop
  completeness/capability extensions, validation problem components, explicit partial
  markers for unknown metadata, and report-only optimization candidates.

These commands deliberately avoid handler execution, HTTP server startup, V8 app loading,
app compilation/extraction, package-manager behavior, live DB checks, native JSON
optimization, route partitioning, and multi-isolate execution by default.

## Low-Level Style Vs Sloppy Style

Primitive-first style:

```ts
serve({
  fetch(req) {
    const url = new URL(req.url);

    if (url.pathname === "/users" && req.method === "POST") {
      return new Response("...");
    }

    return new Response("not found", { status: 404 });
  }
});
```

Sloppy style:

```ts
app.mapPost(
  "/users",
  { body: CreateUser },
  async ({ body, services }) =>
    Results.created("/users/1", await createUser(body, services))
);
```

The Sloppy version exposes method, path, schema, services, result semantics, and future plan
metadata.

## Internal Architecture

Likely implementation areas:

```text
compiler/src/extract/routes/
compiler/src/extract/modules/
compiler/src/plan/
src/core/services/
src/core/diagnostics/
src/core/results/
src/engine/v8/bootstrap/
tests/golden/ergonomics/
tests/integration/ergonomics/
```

Do not create broad directories before their first bounded implementation story.

## Implementation Epics

### Ergonomics API Skeleton

Tasks:

- define tiny TypeScript facade shape;
- document `Sloppy.create()` and `Sloppy.createBuilder()`;
- create compiler fixture that recognizes one tiny app later.

Acceptance:

- tiny app fixture emits one route and one handler in a golden plan;
- public API names match this document or this document is updated with an ADR.

### Results Helpers

Tasks:

- define result descriptor shape;
- implement text/JSON/status helpers in JS bootstrap later;
- implement native conversion later.

Acceptance:

- `Results.text`, `Results.ok`, `Results.notFound`, and `Results.problem` are tested first;
- unsupported result descriptor yields a diagnostic.

### Route Groups

Tasks:

- define group metadata model;
- implement prefix/tag/name contribution;
- validate ambiguous routes.

Acceptance:

- grouped routes emit deterministic paths and names;
- duplicate route diagnostics include group source span.

### Validation Schema API

Tasks:

- define minimal schema DSL;
- emit route binding metadata;
- add automatic validation diagnostics later.

Acceptance:

- body schema appears in plan;
- invalid input produces a problem result when HTTP exists;
- OpenAPI generator has enough schema metadata later.

### Module Ergonomics

Tasks:

- support explicit module registration;
- topologically sort module graph;
- freeze module contributions before run.

Acceptance:

- modules scale the same tiny app model;
- cycles and missing dependencies are diagnostics.

### Data Provider Ergonomic Wrappers

Tasks:

- define provider module shapes;
- implement common query template wrapper later;
- expose provider-specific namespace APIs.

Acceptance:

- query templates lower to parameterized native calls;
- provider-specific APIs do not masquerade as portable.

### CLI Introspection Commands

Tasks:

- define command output contracts for routes, doctor, audit, openapi;
- read plan artifacts;
- avoid running user handlers for introspection.

Acceptance:

- each command has golden output fixtures;
- diagnostics explain missing or stale plan artifacts.

### Diagnostics Polish

Tasks:

- build diagnostic templates for common user mistakes;
- source-map handler errors;
- redact secrets.

Acceptance:

- missing service, duplicate route, missing config, provider unavailable, permission denied,
  and source-mapped exception examples have snapshots.

## Quality Gates

- every ergonomic API addition has a plan fixture or test;
- no public API is added only in documentation without an implementation story;
- examples compile once TypeScript API exists;
- diagnostics snapshots cover failure examples before release claims.

## Open Questions

- Exact package layout for `sloppy` public API.
- Exact validation schema DSL ownership.
- Whether `Sloppy.create()` is v0.1 or later convenience.
- Exact command output formats for plan-powered tools.
## ENGINE-14 Function Modules

ENGINE-14 adds the first compiler-owned multi-file app shape for framework MVP work:
relative function-module imports, explicit SQLite provider imports from
`"sloppy/providers/sqlite"`, Minimal API aliases (`app.get(...)`, `app.group(...)`) in the
supported compiler subset, and Plan-visible provider/capability metadata. Artifact runtime
format remains classic/bootstrap-executed; source-level imports are not a Node/npm
compatibility promise.
