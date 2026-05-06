# Plan Module

## Status

Partially implemented through TASK 06.C and compiler-emitted MVP artifacts.

Post-ENGINE-16 consolidation: Plan metadata is now strong enough to drive routes,
capabilities, providers, source maps, completeness, doctor/audit/OpenAPI, and current
app-host startup validation for the supported subset. ENGINE-27.A/B now stores
`requiredFeatures[]` in the native Plan and maps Plan target/route/provider metadata to the
runtime feature registry. ENGINE-27.C/D adds feature-specific descriptor metadata and uses
the Plan-activated feature set to gate V8 intrinsic registration. ENGINE-27.E/F pins the
missing-feature diagnostic goldens and defines package include-only-used policy without
claiming current compile/link trimming.

## Purpose

Define, and later load and validate, `app.plan.json`, the compiler/runtime contract.

## Scope

Implemented now:

- minimal Sloppy Plan v1 native structs;
- supported version constants and helper;
- target platform/engine string constants;
- handler ID type and invalid/reserved ID rule;
- handler lookup by numeric runtime dispatch ID;
- duplicate handler ID detection;
- valid and invalid handwritten plan fixtures;
- JSON parsing from caller-provided bytes with `yyjson`;
- minimal Plan v1 shape validation;
- native Plan v1 alpha route, data provider, and capability metadata validation when those
  sections are present;
- native Plan v1 `requiredFeatures[]` storage for runtime feature activation;
- runtime feature descriptor metadata for current stdlib imports, provider imports, and
  V8 intrinsic namespaces where implemented;
- CORE-TIME-01.A/B `stdlib.time` feature metadata for the `sloppy/time` import;
- CORE-FS-01.A/B `stdlib.fs` feature metadata for the `sloppy/fs` import;
- CORE-CRYPTO-01.E `stdlib.crypto` feature metadata and V8 intrinsic activation for the
  implemented `sloppy/crypto` random/hash/HMAC/Password/Secret surface;
- arena-owned parsed plan storage;
- basic diagnostics for invalid plan JSON and validation failures;
- documented golden plan fixture matrix;
- compiler-emitted minimal Plan v1 JSON plus a native-validated `routes` section;
- supported-path `sloppy run --artifacts` hash and compatibility checks for `app.js` and
  `app.js.map`.
- source-input `sloppy run <source.js>` / `sloppy run` handoff that compiles first and
  then reuses the same artifact loader and Plan validation path.

MAIN1-10 implemented scope:

- validated database, filesystem, and network capability metadata;
- database capability provider references are required and must point at `dataProviders`;
- filesystem capabilities support read/write/readwrite plus append/delete/list/metadata/
  watch/lock policy categories without provider references; network capabilities remain
  skeleton metadata without provider references;
- provider/capability metadata rejects obvious secret-bearing fields;
- an immutable runtime capability registry can be initialized from a parsed plan.
- SQLite data providers may include canonical `database` metadata used by the V8 SQLite
  bridge when resolving `data.sqlite("name")`.

Future scope:

- file-based loading;
- native host graph construction;
- real module section parsing and validation;
- PostgreSQL/SQL Server JS-to-native provider bridges calling the capability registry
  before provider work.
- post-Core Strong Plan metadata for Minimal API routes, function modules, explicit
  provider imports, inferred capabilities, layered config keys, request binding/schema
  metadata, declared response shapes, and source locations.

## Non-goals

No general file I/O implementation in this Plan module, production route table construction, service model, module model,
provider opening, permission enforcement, source map parser, HTTP runtime broadening, V8
execution changes, JSON serialization inside the C runtime, streaming parser, schema
framework, plugin validator, or package-manager behavior.

Strong Plan extraction recognizes the Slop app DSL and must not become arbitrary
JavaScript magic. Dynamic route/provider/capability/body/response behavior that is not
Plan-visible must fail with diagnostics or require explicit metadata.

TASK 14 exposes bootstrap module debug metadata through `app.__debug().modules`, but this
is not parsed by the native plan loader and is not emitted as `app.plan.json`.
EPIC-15 exposes bootstrap capability debug metadata through
`app.__getPlanContributions().capabilities`, but this is not parsed by the native plan
loader and is not emitted as `app.plan.json`.
EPIC-19 CLI introspection reads plan-compatible JSON files with optional `routes`,
`modules`, `dataProviders`, `capabilities`, and `doctorChecks` sections. MAIN1-11 makes
`routes`, `doctor`, and `openapi` validate their input with `sl_plan_parse_json` before
emitting metadata-derived output. `audit` remains metadata-only and does not execute
application code, but it can inspect intentionally problematic fixtures to report multiple
bounded alpha findings.
EPIC-21 `sloppyc build` emits the first real compiler-owned `routes` metadata section with
`method`, `pattern`, `handlerId`, and `name`. MAIN1-02 makes that section a
native-validated Plan v1 alpha contract. `dataProviders` and `capabilities` are also
native-validated when present. MAIN1-10 adds a runtime capability registry and explicit
check hooks. CORE-FS-01.A/B turns filesystem metadata into the first filesystem API policy
contract; filesystem execution and network APIs remain outside this module.

## Public/Internal API

Implemented public header:

- `include/sloppy/plan.h`

Implemented API:

- `SL_PLAN_VERSION_1` and `SL_PLAN_CURRENT_VERSION`;
- `SL_PLAN_TARGET_PLATFORM_WINDOWS_X64` and `SL_PLAN_TARGET_ENGINE_V8`;
- `SL_PLAN_RUNTIME_MIN_VERSION_0_1_0` and `SL_PLAN_STDLIB_VERSION_0_1_0`;
- `SlHandlerId` and `SL_HANDLER_ID_INVALID`;
- `SlPlanTarget`;
- `SlPlanBundle`;
- `SlPlanSourceMap`;
- `SlPlanHandler`;
- `SlPlan`;
- `sl_plan_version_supported`;
- `sl_handler_id_valid`;
- `sl_plan_find_handler_by_id`;
- `sl_plan_has_duplicate_handler_ids`;
- `SlPlanParseOptions`;
- `sl_plan_parse_json`.

## Ownership/Lifetime Rules

All `SlStr` fields in the TASK 06.A structs are borrowed views. They do not require NUL
termination, do not allocate, and remain valid only for the caller-documented plan
lifetime.

`SlPlan.handlers` is a borrowed, caller-owned array for manually constructed plans.
`sl_plan_find_handler_by_id` returns a borrowed pointer into that array.

`sl_plan_parse_json` copies all parsed strings and the handler array into the supplied
`SlArena`. Parsed plans returned by that API remain valid until the arena is reset or the
caller-owned arena backing buffer ends. No string or handler returned from the parser points
into the `yyjson` document; the parser frees the document before returning.

`SlPlanParseOptions.source_name` is a borrowed diagnostic label. It is copied into
diagnostics when a diagnostic is emitted.

## Invariants

Implemented invariants:

- only schema version 1 is currently supported;
- handler ID 0 is reserved/invalid;
- handler IDs are numeric runtime dispatch keys;
- duplicate handler IDs are invalid plan input;
- handler export names identify future engine bridge exports;
- handler display names are diagnostic/user-facing labels only;
- TASK 08.A uses `exportName` as the V8 global function name for the handwritten execution
  smoke path; this is a temporary smoke convention, not the final handler registration
  protocol.
- helper functions do not allocate, parse JSON, perform file I/O, or call platform/engine
  APIs.

Future loader invariants:

- unsupported schema versions and malformed plans fail before runtime work is served;
- unknown fields are allowed and ignored for forward compatibility;
- `sloppy run --artifacts` verifies `sha256:` bundle/source-map hashes before creating V8
  when artifact bytes are available.
- TASK 10.C synthetic HTTP dispatch keeps route bindings manual, while compiler artifacts
  use native-validated `SlPlan.routes`.
- EPIC-21/24 compiler output assigns handler IDs starting at `1` in source order and emits
  classic-script handlers that register those IDs through `__sloppy_register_handler`.

Parser validation rules:

- the JSON root must be an object;
- `schemaVersion` is required and must equal `1`;
- `compilerVersion`, `runtimeMinimumVersion`, and `stdlibVersion` are required non-empty
  strings;
- `target.platform` and `target.engine` are required non-empty strings;
- target values are shape-validated only; runtime compatibility is deferred;
- `bundle.path`, `bundle.id`, and `bundle.hash` are required non-empty strings;
- `sourceMap.path`, `sourceMap.id`, and `sourceMap.hash` are required non-empty strings;
- `handlers` is required and must be a non-empty array;
- every handler must have a nonzero unsigned integer `id`;
- handler IDs must be unique;
- every handler must have non-empty string `exportName` and `displayName`;
- `routes`, when present, must contain GET route entries with valid native patterns,
  declared handler references, unique method/pattern pairs, and unique non-empty names;
- `dataProviders`, when present, must contain unique valid tokens, supported provider
  values, optional capability references, optional service tokens, and optional canonical
  SQLite `database` metadata;
- `capabilities`, when present, must contain unique valid tokens, supported kind/access
  pairs, required provider references for database capabilities, and no provider reference
  for filesystem/network skeleton capabilities.

Known fields with the wrong JSON type fail validation. Unknown top-level and nested fields
are ignored.

## Diagnostics

The parser emits basic arena-owned diagnostics when `out_diag` is supplied:

- `SL_DIAG_MALFORMED_JSON` for invalid JSON bytes;
- `SL_DIAG_INVALID_PLAN_VERSION` for missing, malformed, or unsupported `schemaVersion`;
- `SL_DIAG_INVALID_PLAN_FIELD` for missing required fields, wrong field types, invalid
  handler IDs, empty strings, and empty handler arrays;
- `SL_DIAG_DUPLICATE_HANDLER_ID` for duplicate handler IDs.

Diagnostics include error severity, a stable code, a short message, an optional source name,
and a simple hint. JSON pointer spans, source frames, and source-map integration are
deferred.

## Tests

CTest registers `tests/unit/core/test_plan.c`, covering:

- current/supported version helper;
- unsupported version rejection by helper;
- valid and invalid handler IDs;
- existing and missing handler lookup;
- null argument behavior;
- empty handler table behavior;
- duplicate handler ID detection;
- availability of checked-in plan fixtures.

CTest registers `tests/unit/core/test_plan_parse.c`, covering:

- parsing all valid fixtures in `tests/golden/plan/`;
- parsed field values, handler IDs, export names, display names, target, bundle, and source
  map values;
- invalid version fixture diagnostics;
- malformed JSON diagnostics;
- missing top-level field, bundle, bundle path, source map, handlers, handler export, and
  empty handler export diagnostics;
- duplicate handler ID diagnostics;
- invalid handler ID diagnostics;
- wrong handler ID type rejection;
- wrong known-field type rejection;
- empty handler array rejection;
- unknown future-field allowance;
- failed parse arena rollback behavior;
- invalid parser arguments.

Fixture files:

- `tests/golden/plan/README.md`;
- `tests/golden/plan/valid-minimal.plan.json`;
- `tests/golden/plan/valid-multiple-handlers.plan.json`;
- `tests/golden/plan/unknown-future-field.plan.json`;
- `tests/golden/plan/malformed-json.plan.json`;
- `tests/golden/plan/invalid-version.plan.json`;
- `tests/golden/plan/missing-runtime-minimum-version.plan.json`;
- `tests/golden/plan/missing-bundle.plan.json`;
- `tests/golden/plan/missing-bundle-path.plan.json`;
- `tests/golden/plan/missing-source-map.plan.json`;
- `tests/golden/plan/missing-handlers.plan.json`;
- `tests/golden/plan/empty-handlers.plan.json`;
- `tests/golden/plan/invalid-handler-id.plan.json`;
- `tests/golden/plan/duplicate-handler-id.plan.json`;
- `tests/golden/plan/missing-handler-export.plan.json`;
- `tests/golden/plan/empty-handler-export.plan.json`;
- `tests/golden/plan/wrong-field-type.plan.json`.

Fixture conventions live in `tests/golden/plan/README.md`. Valid fixtures use the
`valid-*.plan.json` prefix. Invalid fixtures name the rejected condition directly and must
be added to the parser fixture matrix when introduced.

EPIC-19 adds CLI metadata fixtures under `tests/fixtures/cli/`. These are not native Plan
parser fixtures; they are plan-compatible introspection inputs used by process-level
`sloppy routes`, `sloppy doctor`, `sloppy audit`, and `sloppy openapi` golden tests.

## Source Docs

- `docs/app-plan.md`;
- `docs/execution-model.md`;
- `docs/diagnostics.md`;
- `docs/testing-strategy.md`;
- ADR 0004.

## Open Questions

- Exact future source-map parsing policy.
- JSON pointer/source-frame diagnostic strategy.
