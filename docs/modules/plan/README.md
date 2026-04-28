# Plan Module

## Status

Partially implemented for TASK 06.B.

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
- arena-owned parsed plan storage;
- basic diagnostics for invalid plan JSON and validation failures.

Future scope:

- file-based loading;
- runtime compatibility checks;
- hash/source map checks;
- native host graph construction.

## Non-goals

No file I/O, route model, service model, module model, data provider model,
permission/capability model, source map parser, hash verification, HTTP, V8 execution,
compiler extraction, JSON serialization, streaming parser, schema framework, plugin
validator, or package-manager behavior.

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
- helper functions do not allocate, parse JSON, perform file I/O, or call platform/engine
  APIs.

Future loader invariants:

- unsupported schema versions and malformed plans fail before runtime work is served;
- unknown fields are allowed and ignored for forward compatibility;
- bundle/source-map hash verification is a later validation task.

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
- every handler must have non-empty string `exportName` and `displayName`.

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

- parsing the minimal valid fixture;
- parsed field values and handler lookup;
- invalid version fixture diagnostics;
- missing bundle and source map fixture diagnostics;
- missing handler export diagnostics;
- duplicate handler ID diagnostics;
- malformed JSON diagnostics;
- wrong handler ID type rejection;
- handler ID `0` rejection;
- empty handler array rejection;
- unknown field allowance;
- invalid parser arguments.

Fixture files:

- `tests/golden/plan/minimal-valid.plan.json`;
- `tests/golden/plan/invalid-version.plan.json`;
- `tests/golden/plan/duplicate-handler-id.plan.json`;
- `tests/golden/plan/missing-bundle.plan.json`;
- `tests/golden/plan/missing-source-map.plan.json`;
- `tests/golden/plan/missing-handler-export.plan.json`.

## Source Docs

- `docs/app-plan.md`;
- `docs/execution-model.md`;
- `docs/diagnostics.md`;
- `docs/testing-strategy.md`;
- ADR 0004.

## Open Questions

- Exact hash verification policy.
- Runtime compatibility policy for target platform, target engine, runtime minimum version,
  and stdlib version.
- JSON pointer/source-frame diagnostic strategy.
