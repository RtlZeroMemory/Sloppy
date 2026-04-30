# Sloppy Modularity and Extension Model

## Purpose

This document defines how Sloppy applications are composed and extended. Sloppy
extensibility starts with TypeScript app modules, not native plugins.

Modules must be declarative, phased, compiler-readable, plan-emitting, deterministic, and
auditable.

## Scope

This document covers:

- extension layers;
- app module API;
- exact planned module phases;
- module dependency graph and topological sorting;
- cycle diagnostics;
- graph freeze;
- service token strategy;
- capability injection;
- middleware, endpoint filters, and result filters;
- compiler extraction expectations;
- Sloppy Plan contribution;
- dynamic mode behavior;
- future native plugins;
- future compiler plugins;
- tests and acceptance criteria.

## Non-Goals

The foundation phase does not implement:

- app module runtime;
- module compiler extraction;
- graph freeze;
- native plugin loading;
- compiler plugin API;
- dynamic module behavior.

## Current Phase

The bootstrap stdlib implements the first JavaScript-only module skeleton. `Sloppy.module`
creates module definitions; `builder.addModule` registers them; `builder.build` validates
missing dependencies and cycles, sorts modules deterministically, runs capabilities before
services before routes, attributes module-created capabilities/services/routes, and exposes
plan-like debug metadata through `app.__debug().modules`.

This is not compiler extraction, real `app.plan.json` emission, native runtime module
loading, package distribution, or native plugin support.

## Future Phase

The first implementation should support a tiny declarative module graph with deterministic
ordering and plan contribution before dynamic behavior exists.

## Extension Layers

Layers have different stability and safety rules:

| Layer | Purpose | Stability | Safety boundary |
| --- | --- | --- | --- |
| App modules | User-facing TypeScript composition | public API, early | compiler-readable TS |
| First-party feature modules | Official auth/data/files/etc. packages | public API when released | Sloppy module API |
| Built-in compiler extractors | Plan metadata extraction | internal | Rust compiler code |
| Compiler plugins | Future metadata extensions | future public API | restricted/deterministic |
| Native providers | Future native capability/provider code | future ABI | Sloppy-owned ABI |
| Engine backends | V8 first, future engines possible | internal/backend | no engine types leak |
| Runtime backends | event loop/platform/protocol | internal/backend | Sloppy abstractions |

Native plugins are not the starting point. They are future work after the app module and
provider boundaries are proven.

## Public API Shape

Planned module:

```ts
export const UsersModule = Sloppy.module("users")
  .dependsOn("data")
  .services(services => {
    services.addSingleton("users.message", () => "hello");
  })
  .routes(app => {
    const users = app.mapGroup("/users").withTags("Users");

    users.mapGet("/{id:int}", ({ route, services }) => {
      return Results.ok({
        id: route.id ?? "demo",
        message: services.get("users.message"),
      });
    })
      .withName("Users.Get");
  });
```

Root app:

```ts
const builder = Sloppy.createBuilder();

builder
  .addModule(SqliteModule.configure({
    token: "data.main",
    database: "app.db",
  }))
  .addModule(UsersModule);

const app = builder.build();

export default app;
```

`app.run()` remains deferred by the ENGINE-01 contract. Runtime startup uses compiled
artifacts through `sloppy run --artifacts` until source-input handoff is implemented.

## Module Phases

Exact planned phases:

1. config;
2. capabilities;
3. permissions;
4. services;
5. middleware;
6. endpoint filters;
7. result filters;
8. routes;
9. jobs;
10. health checks;
11. metadata;
12. validation/freeze.

Phase rules:

- config is available before provider/service construction;
- capabilities are declared before permissions reference them;
- services are declared before routes require them;
- middleware/filter ordering is deterministic;
- routes are validated after group metadata is applied;
- freeze occurs after validation and before run.

Current bootstrap TASK 14 phases are intentionally smaller:

1. dependency resolution / graph validation;
2. capabilities callbacks for each module in dependency order;
3. services callbacks for each module in dependency order;
4. routes callbacks for each module in dependency order;
5. debug metadata assembly for module names, dependencies, capabilities, services, routes,
   and custom metadata.

Config, permissions, middleware, filters, jobs, health checks, validation freeze, and native
graph freeze are future phases. Capabilities exist only as bootstrap metadata today.

## Module Dependency Graph

Modules may declare dependencies by module name:

```ts
export const UsersModule = Sloppy.module("users")
  .dependsOn("data")
  .services(...)
  .routes(...);
```

Compiler/runtime requirements:

- collect all module declarations;
- verify dependency names exist;
- topologically sort modules;
- use deterministic tie-breaking when independent modules exist;
- diagnose cycles with the full cycle path;
- never let import order silently decide behavior.

Current bootstrap behavior validates dependency names at build time, fails missing
dependencies with the module and dependency name, fails cycles with the cycle path, and
uses builder insertion order as the deterministic tie-break for independent modules.

Cycle diagnostic example:

```text
error[SLP_MODULE_CYCLE]: module dependency cycle detected

  Cycle:
    auth -> users -> billing -> auth
```

## App Graph Freeze

After `builder.build()`:

- route graph is frozen;
- service graph is frozen;
- middleware/filter order is frozen;
- permissions/capabilities are frozen;
- handler IDs are stable;
- plan emission is complete.

This enables:

- native route trie or equivalent table;
- stable handler IDs;
- service graph validation;
- permission audit;
- OpenAPI generation later;
- better performance;
- deterministic diagnostics.

## Service Tokens

Namespaced string tokens are first:

- `data.main`;
- `users.repo`;
- `auth.session`;
- `logging.sink.console`.

Rules:

- tokens are case-sensitive;
- module-owned tokens should use module prefix;
- provider tokens must be plan-visible;
- duplicate tokens are diagnostics unless explicitly overriding is a future feature.

Typed service tokens are a future ergonomic improvement. String tokens remain useful in
plans, diagnostics, and native boundaries.

## Capability Injection

Prefer capability injection over global power:

```ts
export const FilesModule = Sloppy.module("files")
  .capabilities(caps => {
    caps.addDir("files.storage", "./uploads", {
      read: true,
      write: true,
    });
  })
  .services(services => {
    services.addScoped("files.storage", scope => {
      return scope.capabilities.dir("files.storage");
    });
  });
```

This enables `sloppy audit` to explain authority flow from modules to services to routes.

## Middleware, Endpoint Filters, Result Filters

Definitions:

- middleware: global or group-level request pipeline;
- endpoint filters: per-route or per-group pre-handler checks;
- result filters: post-handler, pre-response transforms.

Plan sections must keep them separate because ordering, applicability, diagnostics, and
performance differ.

## Compiler Extraction Expectations

Static plan extraction should capture:

- module name;
- dependencies;
- phase callbacks used;
- service tokens and lifetimes;
- route groups and routes;
- handler IDs and user-facing names;
- middleware/filter declarations;
- capability and permission declarations;
- data provider registrations;
- health checks and jobs later;
- source locations for every contribution.

Static mode rejects or warns on dynamic expressions that prevent plan extraction.

## Dynamic Mode Behavior

Dynamic mode is future and explicit. It may allow runtime route/module changes, but must:

- be opt-in;
- appear in the Sloppy Plan;
- reduce optimization claims;
- reduce introspection guarantees;
- produce warnings when tooling output is incomplete.

No implementation story should accidentally introduce dynamic mode.

## App Plan Contribution

Modules contribute to plan sections:

- `modules`;
- `routes`;
- `handlers`;
- `services`;
- `middleware`;
- `filters`;
- `capabilities`;
- `permissions`;
- `dataProviders`;
- `healthChecks`;
- diagnostics metadata.

Illustrative module entry:

```json
{
  "name": "users",
  "dependencies": ["data"],
  "order": 20,
  "contributes": ["routes", "services", "permissions"],
  "source": {
    "file": "users.module.ts",
    "line": 1,
    "column": 14
  }
}
```

Current TASK 14 does not emit this JSON. Instead, bootstrap apps expose debug metadata:

```js
app.__debug().modules
```

Each entry contains `name`, `dependencies`, `order`, `contributes`, `capabilities`,
`services`, `routes`, and `metadata`. This shape is a temporary introspection/debug
contract for tests and future plan work, not the final Sloppy Plan schema.

MAIN1-03 does not add native module execution or a Plan `modules` section. The only
service metadata currently represented in native startup validation is
`dataProviders[].service`; when present, those string tokens are syntax-checked and must be
unique. Bootstrap module dependencies, duplicate module names, cycles, and service
registrations remain JavaScript-only diagnostics until compiler extraction emits native
module/service metadata.

## Native Plugins

Native plugins are future work, not v0.1.

Rules:

- no direct V8 exposure;
- Sloppy-owned ABI;
- versioned ABI;
- resource table integration;
- permission integration;
- dynamic loading behind platform abstraction;
- first-party static providers before third-party dynamic loading.

Conceptual shape, not final ABI:

```c
typedef struct SlPluginHostV1 {
    uint32_t abi_version;

    SlStatus (*register_service_provider)(...);
    SlStatus (*register_native_function)(...);
    SlStatus (*register_resource_kind)(...);
    SlStatus (*register_permission_kind)(...);

    void *reserved[32];
} SlPluginHostV1;

SL_PLUGIN_EXPORT SlStatus sl_plugin_init(
    const SlPluginHostV1 *host,
    SlPluginRegistration *registration
);
```

## Compiler Plugins

Compiler plugins are future work, not v0.1.

They should be:

- versioned;
- deterministic;
- metadata-focused first;
- restricted/sandboxed where possible;
- denied arbitrary filesystem/network access by default.

Initial `sloppyc` uses built-in extractors only.

## Future Package Manifest

Future package shape:

```json
{
  "name": "@sloppy/cors",
  "version": "0.1.0",
  "sloppy": {
    "module": "./dist/module.js",
    "plan": {
      "requiresCompiler": ">=0.1.0",
      "contributes": ["middleware", "permissions"]
    },
    "native": null
  }
}
```

Future native package:

```json
{
  "name": "@sloppy/postgres-native",
  "version": "0.1.0",
  "sloppy": {
    "module": "./dist/module.js",
    "native": {
      "windows-x64": "./native/sloppy_postgres.dll",
      "linux-x64": "./native/libsloppy_postgres.so"
    },
    "abi": "sloppy-native-1"
  }
}
```

This manifest is future work, not v0.1.

## Internal Architecture

Likely future layout:

```text
compiler/src/extract/modules/
compiler/src/plan/
src/modules/
include/sloppy/modules.h
tests/golden/modules/
tests/integration/modules/
```

Do not create implementation directories until their first test-backed story.

## Lifecycle Flow

Target lifecycle:

1. collect builder modules;
2. collect module dependencies;
3. topologically sort;
4. execute/extract phases in order;
5. validate contributions;
6. emit plan entries;
7. runtime validates plan;
8. runtime freezes graph;
9. app runs with no further graph mutation in static mode.

## Error And Diagnostic Behavior

Diagnostics must include:

- module cycle;
- missing dependency;
- duplicate module name;
- duplicate service token;
- dynamic route in static mode;
- missing permission/capability;
- provider token missing;
- invalid phase ordering.

Diagnostics should include module name, phase, source span, and suggested fix where safe.
Current bootstrap diagnostics are plain JavaScript errors rather than native `SlDiag`
records. They include duplicate module names, invalid module objects, missing dependencies,
cycles, invalid module names, duplicate capability tokens, invalid capability tokens,
missing capabilities, mutation after module add, and phase callback failures.
Native MAIN1-03 diagnostics cover duplicate represented provider service tokens but do not
yet diagnose bootstrap module names, module dependencies, service lifetimes, or module
cycles from Plan metadata because that metadata is not emitted.

## Testing Requirements

Tests required:

- compiler golden tests for module plan output;
- cycle diagnostics;
- deterministic ordering;
- duplicate token diagnostics;
- graph freeze behavior;
- permission contribution fixtures;
- dynamic mode rejection/warning tests.

## Quality Gates

- module schema fixture updates accompany extraction changes;
- diagnostics snapshots cover graph errors;
- static mode never silently accepts unextractable graph behavior;
- no native plugin ABI lands before an ADR/task explicitly starts that phase.

## Implementation Phases

### Phase A: Spec And Plan Fixtures

Tasks:

- add sample plan fixtures with modules;
- define module entry fields;
- document diagnostics.

Acceptance:

- full plan sample includes modules and dependencies;
- invalid cycle fixture exists.

### Phase B: Fake Module Plan Emitter

Tasks:

- teach fake emitter to produce two ordered modules;
- include one service and one route contribution.

Acceptance:

- golden output is deterministic;
- no Oxc extraction is required.

### Phase C: One Module Extractor

Tasks:

- extract `Sloppy.module("name")`;
- extract `.dependsOn("...")`;
- extract one route from `.routes(...)`.

Acceptance:

- literal module names work;
- nonliteral module name produces diagnostic;
- source span is preserved.

### Phase D: Dependency Sort

Tasks:

- implement topological sort;
- deterministic tie-break;
- cycle diagnostic.

Acceptance:

- cycle test reports full cycle;
- independent modules have stable order.

### Phase E: Graph Freeze

Tasks:

- freeze service/route/middleware contributions;
- reject mutation after `builder.build()`;
- expose frozen graph to plan/runtime.

Acceptance:

- mutation after build fails in tests;
- runtime route table sees stable handler IDs.

## Acceptance Criteria

Module foundation is accepted when:

- plan schema supports module entries and deterministic order;
- compiler can fake or extract module entries;
- cycles and missing dependencies are tested diagnostics;
- graph freeze semantics are implemented before dynamic mode;
- no native plugin API is required for app modules.

## Open Questions

- Exact typed token API.
- Whether module names are package-scoped.
- Whether dynamic mode is allowed before v0.1.
- How module versioning appears in plan v1.
- How module package manifests align with TypeScript package managers.
