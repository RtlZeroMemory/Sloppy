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

- file-based plan loading;
- route matching;
- service graph execution;
- module extraction;
- database providers.

## Current Phase

TASK 06.B implements JSON parsing and minimal shape validation for handwritten Plan v1
bytes. The parser exposes an arena-owned `SlPlan` and handler table to later runtime work.
The runtime still does not load a plan from disk, execute JavaScript, build routes, verify
hashes, or run handlers.

## Public API Shape

The plan is not normally user-authored. It is a public compatibility contract for `sloppyc`,
`sloppy`, diagnostics tools, and future package tooling. Users may inspect it through future
commands such as `sloppy audit`, `sloppy routes`, and `sloppy doctor`.

## Versioning Rules

- `schemaVersion` is mandatory in JSON fixtures.
- Runtime must reject plans with unsupported major schema versions.
- Runtime may accept compatible minor additions only if unknown fields are explicitly
  allowed by that schema version.
- Plan must declare runtime minimum version. Feature requirements are future schema work.
- Plan must declare target platform and target engine.
- Compiler version is diagnostic metadata and cache input.
- TASK 06.A supports only schema version `1` through `sl_plan_version_supported`.

## Implemented Minimal Plan v1 Shape

The implemented native C shape in `include/sloppy/plan.h` is deliberately small:

- `schemaVersion` / `SlPlan.version`;
- `compilerVersion` / `SlPlan.compiler_version`;
- `runtimeMinimumVersion` / `SlPlan.runtime_min_version`;
- `stdlibVersion` / `SlPlan.stdlib_version`;
- `target.platform` and `target.engine`;
- `bundle.path`, `bundle.id`, and `bundle.hash`;
- `sourceMap.path`, `sourceMap.id`, and `sourceMap.hash`;
- `handlers[].id`, `handlers[].exportName`, and `handlers[].displayName`.

All native `SlStr` fields are borrowed views in the struct model. Manually constructed
plans borrow caller-owned storage. `sl_plan_parse_json` copies JSON strings and handler
arrays into the supplied arena, so parsed plan lifetime is tied to that arena rather than
to the JSON parser document.

Handler rules implemented now:

- handler IDs are numeric runtime dispatch keys;
- handler ID `0` is reserved and invalid;
- duplicate handler IDs are invalid plan input;
- handler export names are future engine bridge export names;
- display names are diagnostic/user-facing only.

Unknown JSON fields are allowed and ignored in Plan v1 for forward compatibility. Known
fields with the wrong JSON type fail validation.

## Schema Sections

### metadata

Build identity and provenance:

- plan schema version;
- compiler version.

### target

Runtime compatibility:

- target platform;
- target engine.

### bundle

Executable JavaScript artifact:

- path;
- bundle ID;
- hash.

### sourceMap

Diagnostic mapping artifact:

- path;
- source map ID;
- hash.

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
- user-facing name.

The remaining sections below are future schema sections and are not represented in TASK
06.A native structs.

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

## Minimal Plan v1 Fixture

Current handwritten fixture shape:

```json
{
  "schemaVersion": 1,
  "compilerVersion": "sloppyc-placeholder",
  "runtimeMinimumVersion": "0.1.0",
  "stdlibVersion": "0.1.0",
  "target": {
    "platform": "windows-x64",
    "engine": "v8"
  },
  "bundle": {
    "path": ".sloppy/app.js",
    "id": "app-js-test",
    "hash": "test-only"
  },
  "sourceMap": {
    "path": ".sloppy/app.js.map",
    "id": "app-js-map-test",
    "hash": "test-only"
  },
  "handlers": [
    {
      "id": 1,
      "exportName": "__sloppy_handler_1",
      "displayName": "Home"
    }
  ]
}
```

## Future Full Plan

Later schema phases may add routes, modules, services, middleware, filters, schemas, data
providers, capabilities, permissions, diagnostics metadata, and build cache metadata. Those
sections are intentionally absent from TASK 06.A.

Illustrative future shape, not implemented:

```json
{
  "schemaVersion": 1,
  "compilerVersion": "sloppyc-placeholder",
  "runtimeMinimumVersion": "0.1.0",
  "stdlibVersion": "0.1.0",
  "target": {
    "platform": "windows-x64",
    "engine": "v8"
  },
  "bundle": {
    "path": ".sloppy/app.js",
    "id": "app-js-placeholder",
    "hash": "sha256-placeholder"
  },
  "sourceMap": {
    "path": ".sloppy/app.js.map",
    "id": "app-js-map-placeholder",
    "hash": "sha256-placeholder"
  },
  "handlers": [
    {
      "id": 100,
      "exportName": "__sloppy_handler_100",
      "displayName": "Users.Get"
    }
  ],
  "routes": [],
  "modules": [],
  "services": [],
  "dataProviders": []
}
```

## Validation Rules

Current TASK 06.B parser validation checks:

- JSON root is an object;
- `schemaVersion` exists and equals supported Plan v1;
- `compilerVersion`, `runtimeMinimumVersion`, and `stdlibVersion` exist and are non-empty
  strings;
- `target.platform` and `target.engine` exist and are non-empty strings;
- `bundle.path`, `bundle.id`, and `bundle.hash` exist and are non-empty strings;
- `sourceMap.path`, `sourceMap.id`, and `sourceMap.hash` exist and are non-empty strings;
- `handlers` exists and is a non-empty array;
- every handler has nonzero unsigned integer `id`;
- handler IDs are unique;
- every handler has non-empty string `exportName` and `displayName`;
- malformed JSON fails with a diagnostic rather than crashing.

Current TASK 06.B parser validation deliberately does not check target compatibility,
runtime minimum version compatibility, stdlib availability, bundle hash contents, source map
hash contents, or any future route/module/service/provider sections.

Future plan validation must check:

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

TASK 06.A provided helper coverage for supported version checks, handler ID validity,
handler lookup, and duplicate handler ID detection. TASK 06.B adds minimal JSON parser
validation on top of those helpers.

## Runtime Compatibility Rules

Runtime rejects a plan when:

- runtime version is below `runtimeMinimumVersion`;
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

Current TASK 06.B parser diagnostics are intentionally smaller:

- `SL_DIAG_MALFORMED_JSON`;
- `SL_DIAG_INVALID_PLAN_VERSION`;
- `SL_DIAG_INVALID_PLAN_FIELD`;
- `SL_DIAG_DUPLICATE_HANDLER_ID`.

Diagnostics include error severity, stable code, message, optional source name, and a simple
hint. JSON pointer spans, source frames, source maps, and rendered code frames are deferred.

## Testing Requirements

Plan tests should include:

- valid minimal plan;
- unsupported schema version;
- missing bundle;
- missing source map;
- duplicate handler ID;
- missing handler export.

Future parser/validator phases should add:

- route missing handler;
- module cycle;
- invalid provider token;
- permissions referencing unknown capability;
- golden full plan fixture.

TASK 06.B parser tests cover the existing minimal valid and invalid fixtures plus embedded
malformed JSON, wrong handler ID type, handler ID `0`, empty handlers, and unknown field
allowance.

## Quality Gates

- plan fixture tests run in CTest;
- golden plan fixtures are stable;
- malformed plans produce diagnostics, not crashes;
- no secrets appear in checked-in plan fixtures;
- plan schema changes update this document and ADRs when architectural.

## Implementation Tasks

- Define C structs for parsed plan model.
- Add minimal plan fixture directory.
- Choose JSON parser in the appropriate phase. Done for TASK 06.B with `yyjson`.
- Add validator with section-specific diagnostics. Started for minimal v1 shape.
- Add handler table extraction. Done for minimal v1 handlers.
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
