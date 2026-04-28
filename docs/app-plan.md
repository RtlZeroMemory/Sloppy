# Sloppy Plan

## Purpose

`app.plan.json` is the compiler/runtime contract. It describes the native host graph that
the runtime validates and builds before user code handles work.

The plan exists so the runtime can dispatch routes natively, audit permissions, validate
configuration, check runtime compatibility, and call JavaScript handlers by numeric ID.

## Scope

This document defines the intended plan v1 shape, validation rules, compatibility rules,
compiler emission rules, implementation tasks, and acceptance criteria for the first plan
loader.

## Non-Goals

The foundation phase does not implement:

- a JSON parser;
- plan loading;
- route matching;
- service graph execution;
- module extraction;
- database providers.

## Current Phase

The schema is illustrative and still design-level. Future implementation must turn it into
versioned test fixtures and validation code.

## Future Phase

The first plan loader should read a small handwritten plan, validate metadata, and expose a
handler table to the runtime.

## Public API Shape

The plan is not normally user-authored. It is a public compatibility contract for `sloppyc`,
`sloppy`, diagnostics tools, and future package tooling. Users may inspect it through future
commands such as `sloppy audit`, `sloppy routes`, and `sloppy doctor`.

## Versioning Rules

- `schemaVersion` is mandatory.
- Runtime must reject plans with unsupported major schema versions.
- Runtime may accept compatible minor additions only if unknown fields are explicitly
  allowed by that schema version.
- Plan must declare runtime minimum version and feature requirements.
- Plan must declare target platform and target engine.
- Compiler version is diagnostic metadata and cache input.

## Schema Sections

### metadata

Build identity and provenance:

- app name;
- plan schema version;
- compiler name/version;
- generated timestamp if reproducibility policy allows it;
- build ID;
- diagnostic metadata.

### target

Runtime compatibility:

- minimum runtime version;
- target platform;
- target engine;
- required runtime features;
- bootstrap stdlib version.

### bundle

Executable JavaScript artifact:

- path;
- bundle ID;
- hash;
- module format;
- expected bootstrap ABI.

### sourceMap

Diagnostic mapping artifact:

- path;
- hash;
- original source root policy;
- availability requirement.

### modules

App module graph:

- module name;
- optional package/version;
- dependency list;
- deterministic order;
- contributed phases;
- source location.

### routes

Native route table input:

- route ID;
- method;
- path pattern;
- handler ID;
- group metadata;
- filters;
- middleware references;
- permissions;
- source location.

### handlers

Handler table contract:

- numeric handler ID;
- generated export or registration name;
- user-facing name;
- source location;
- expected signature metadata;
- result metadata if known.

### services

Service graph declarations:

- token;
- lifetime;
- module;
- dependencies;
- source location.

### middleware

Global or group pipeline entries:

- ID;
- module;
- order;
- handler/filter reference;
- applies-to metadata.

### filters

Endpoint and result filters:

- endpoint filters for pre-handler checks;
- result filters for post-handler, pre-response transforms;
- order and scope.

### dataProviders

Database provider registrations:

- token;
- provider name;
- module;
- lifetime;
- config key references;
- required driver/package metadata;
- health check metadata.

### capabilities

Capability declarations:

- token;
- kind;
- access mode;
- provider/source module;
- config key references;
- path policy where applicable.

### permissions

Permission requirements:

- route/service/module reachability;
- database permissions;
- filesystem permissions;
- environment permissions;
- future native plugin permissions.

### diagnostics metadata

Data used for better errors:

- source locations;
- generated locations;
- source map links;
- user-facing names;
- suggested fix metadata when available.

### schemas

Validation and OpenAPI metadata:

- schema ID;
- kind;
- route binding usage;
- source module;
- source location;
- JSON-schema-like shape or Sloppy-specific compact representation;
- diagnostics display name.

## File And Module Layout

Likely implementation areas:

```text
include/sloppy/plan.h
src/core/plan.c
src/core/plan_validate.c
tests/golden/plan/
tests/integration/plan/
compiler/src/plan/
```

Do not create parser or validator files without tests.

## Sample Full Plan

Illustrative, not final:

```json
{
  "schemaVersion": 1,
  "metadata": {
    "appName": "example",
    "buildId": "dev-placeholder",
    "compiler": {
      "name": "sloppyc",
      "version": "0.0.0-foundation"
    }
  },
  "target": {
    "minRuntimeVersion": "0.1.0",
    "platform": "windows-x64",
    "engine": "v8",
    "features": ["handler-ids", "native-routing"],
    "stdlibVersion": "0.1.0"
  },
  "bundle": {
    "path": "app.js",
    "id": "app-js-placeholder",
    "hash": "sha256-placeholder",
    "moduleFormat": "esm",
    "bootstrapAbi": "sloppy-js-bootstrap-1"
  },
  "sourceMap": {
    "path": "app.js.map",
    "hash": "sha256-placeholder",
    "required": true
  },
  "modules": [
    {
      "name": "data",
      "order": 10,
      "dependencies": [],
      "contributes": ["services", "dataProviders", "permissions"]
    },
    {
      "name": "users",
      "order": 20,
      "dependencies": ["data"],
      "contributes": ["routes", "services", "permissions"]
    }
  ],
  "routes": [
    {
      "id": 1,
      "method": "GET",
      "path": "/users/{id:int}",
      "handlerId": 100,
      "name": "Users.Get",
      "filters": [],
      "middleware": [],
      "permissions": ["data.main.read"],
      "source": {
        "file": "users.ts",
        "line": 12,
        "column": 5
      }
    }
  ],
  "handlers": [
    {
      "id": 100,
      "export": "__sloppy_handler_100",
      "name": "Users.Get",
      "source": {
        "file": "users.ts",
        "line": 12,
        "column": 31
      }
    }
  ],
  "handlerTable": {
    "expectedIds": [100],
    "registrationMode": "startup-verified"
  },
  "services": [
    {
      "token": "data.main",
      "lifetime": "singleton",
      "module": "data"
    }
  ],
  "middleware": [],
  "filters": [],
  "schemas": [
    {
      "id": "schema.createUser",
      "name": "CreateUser",
      "kind": "object",
      "properties": {
        "name": {
          "type": "string",
          "minLength": 1
        },
        "email": {
          "type": "string",
          "format": "email"
        }
      },
      "source": {
        "file": "users.ts",
        "line": 4,
        "column": 7
      }
    }
  ],
  "dataProviders": [
    {
      "token": "data.main",
      "provider": "sqlite",
      "module": "data",
      "lifetime": "singleton-pool",
      "config": {
        "pathKey": "DATABASE_PATH"
      }
    }
  ],
  "capabilities": [
    {
      "token": "data.main",
      "kind": "database",
      "provider": "sqlite"
    }
  ],
  "permissions": {
    "database": [
      {
        "token": "data.main",
        "provider": "sqlite",
        "module": "users"
      }
    ]
  },
  "diagnostics": {
    "sourceMap": "app.js.map",
    "sources": [
      {
        "file": "users.ts",
        "displayPath": "users.ts"
      }
    ]
  },
  "buildCache": {
    "sourceHash": "sha256-placeholder",
    "sloppycVersion": "0.0.0-foundation",
    "oxcVersion": "planned",
    "targetEngine": "v8",
    "targetPlatform": "windows-x64",
    "stdlibVersion": "0.1.0"
  }
}
```

## Validation Rules

Plan validation must check:

- schema version is supported;
- target platform and engine are supported;
- required features are known;
- bundle and source map fields are present;
- module dependency graph has no cycles;
- module order is deterministic;
- route IDs are unique;
- handler IDs are unique;
- every route handler ID exists;
- every expected handler export/registration name is present during startup;
- route ambiguity is rejected before serving;
- service tokens are unique within allowed scopes;
- data provider tokens match services/capabilities;
- permissions reference known capabilities;
- schema references point to known schemas;
- unsupported dynamic mode is rejected unless explicitly enabled;
- source map presence follows target mode policy;
- no secret values are embedded in plan config.

## Runtime Compatibility Rules

Runtime rejects a plan when:

- runtime version is below `minRuntimeVersion`;
- target engine is not available;
- target platform is incompatible;
- required stdlib version is unavailable;
- required feature is unknown;
- bundle hash does not match;
- expected handler table is not registered.

## Compiler Emission Rules

`sloppyc` must:

- emit deterministic JSON ordering where practical;
- include source locations for user-facing declarations;
- include stable numeric handler IDs;
- avoid secret values;
- emit source map links;
- validate static plan restrictions before writing success artifacts;
- produce golden-testable output.

## Error And Diagnostic Behavior

Plan diagnostics should identify:

- failing section;
- offending token/route/module/handler;
- source location when available;
- machine-readable diagnostic code;
- suggested fix when safe.

Examples:

- duplicate handler ID;
- route references missing handler;
- module dependency cycle;
- missing config key for provider;
- incompatible runtime feature.

## Testing Requirements

Plan tests should include:

- valid minimal plan;
- unsupported schema version;
- missing bundle;
- missing source map;
- duplicate handler ID;
- route missing handler;
- module cycle;
- invalid provider token;
- permissions referencing unknown capability;
- golden full plan fixture.

## Quality Gates

- plan fixture tests run in CTest;
- golden plan fixtures are stable;
- malformed plans produce diagnostics, not crashes;
- no secrets appear in checked-in plan fixtures;
- plan schema changes update this document and ADRs when architectural.

## Implementation Tasks

- Define C structs for parsed plan model.
- Choose JSON parser in the appropriate phase.
- Add minimal plan fixture directory.
- Add validator with section-specific diagnostics.
- Add handler table extraction.
- Add plan/bundle consistency checks.
- Add golden tests for JSON fixtures.
- Add CTest integration for valid and invalid plans.
- Add schema section fixtures before validation API lands.
- Add dynamic mode rejection fixture.

## Acceptance Criteria For Plan v1 Loader

Plan v1 loader is accepted when:

- runtime can load a minimal handwritten plan;
- validator rejects malformed required sections;
- handler table is exposed to later runtime code;
- diagnostics identify section and source location where available;
- tests cover success and failure fixtures;
- no V8, HTTP, compiler extraction, or database provider code is required.

## Open Questions

- Exact numeric ID allocation strategy.
- Whether plan JSON permits comments in dev mode.
- Whether unknown fields are warnings or errors in v1.
- Exact source location representation for generated module contributions.
