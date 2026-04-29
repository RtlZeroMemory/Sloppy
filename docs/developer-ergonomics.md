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

The repository contains placeholder CLIs and a source-controlled bootstrap stdlib layout.
TASK 11.B/11.C added the first tiny public JavaScript facade inside that stdlib:
`Results.text(...)`, `Results.json(...)`, `Sloppy.create()`, and `app.mapGet(...)`.
TASK 12.A/12.B/12.C/12.D extends that facade with the first app-host foundation skeleton:
`Sloppy.createBuilder()`, `builder.build()`, structural `app.freeze()`, object-backed
config, deterministic memory logging, and string-token singleton/transient services.
TASK 13.A/13.B/13.C/13.D extends the same bootstrap facade with in-memory route groups,
the fuller bounded `Results.*` helper set, a small `schema` validation skeleton, and an
`examples/ergonomics/` API-shape fixture.
This facade is still in-memory and conceptual only. It does not run an app, emit a Sloppy
Plan, serve HTTP, perform compiler extraction, validate requests, or integrate modules.
`examples/hello/` demonstrates the checked-in bootstrap API shape through a relative import
from `stdlib/sloppy/index.js` because the public bare `"sloppy"` specifier remains future
compiler/runtime behavior.

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

await app.run();
```

Acceptance criteria for this API shape:

- one import gives the core app API and results helpers;
- `Sloppy.create()` is equivalent to a default builder plus `build()`;
- route declaration remains compiler-readable;
- handler receives a typed context only when it asks for one;
- emitted plan contains one route and one handler ID.

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

TASK 11.D adds the first checked-in tiny app example:

```text
examples/hello/
  README.md
  app.js
```

That example is static bootstrap validation only. It is not compiled by `sloppyc`, does not
emit `app.plan.json`, and does not serve HTTP.

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
- future native app-host work will make build/freeze part of startup validation and plan
  emission;
- errors detected before `run()` should be startup diagnostics, not request-time surprises.

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

## Data Provider Ergonomics

Database providers are modules.

SQLite:

```ts
import { sqlite } from "sloppy:data/sqlite";

builder.addModule(
  sqlite.module({
    token: "data.main",
    path: "app.db",
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

Future CLI ergonomics should be powered by `app.plan.json`.

| Command | Purpose | Planned output | Plan sections |
| --- | --- | --- | --- |
| `sloppy routes` | Inspect route graph | methods, paths, names, handlers, middleware | `routes`, `handlers`, `middleware`, `filters` |
| `sloppy doctor` | Check environment and providers | missing drivers, invalid config, SDK issues | `target`, `dataProviders`, `bundle` |
| `sloppy audit` | Explain permissions/capabilities | capability grants and reachable routes | `capabilities`, `permissions`, `routes`, `modules` |
| `sloppy openapi` | Generate API description | OpenAPI document | `routes`, `schemas`, `metadata` |
| `sloppy db migrate` | Future migration runner | migration plan/status | `dataProviders`, provider metadata |
| `sloppy db status` | Database health/config status | connection/provider status | `dataProviders`, `healthChecks` |
| `sloppy new api` | Create project skeleton | app scaffold | templates, not plan-dependent |
| `sloppy check` | Type and plan validation | diagnostics | compiler graph, plan metadata |
| `sloppy build` | Emit artifacts | `.sloppy/` output | all artifact sections |
| `sloppy run` | Build/cache/run | runtime startup diagnostics | all artifact sections |

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
