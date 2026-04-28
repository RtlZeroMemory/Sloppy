# Plan Module

## Status

Partially implemented for TASK 06.A.

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
- valid and invalid handwritten plan fixtures.

Future scope:

- JSON loading;
- validation diagnostics;
- hash/source map checks;
- native host graph construction.

## Non-goals

No JSON parser, JSON validator, file I/O, route model, service model, module model, data
provider model, permission/capability model, source map parser, hash verification, HTTP,
V8 execution, compiler extraction, or package-manager behavior.

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
- `sl_plan_has_duplicate_handler_ids`.

## Ownership/Lifetime Rules

All `SlStr` fields in the TASK 06.A structs are borrowed views. They do not require NUL
termination, do not allocate, and remain valid only for the caller-documented plan
lifetime.

`SlPlan.handlers` is a borrowed, caller-owned array. `SlPlan` never allocates, copies, or
frees handler storage. `sl_plan_find_handler_by_id` returns a borrowed pointer into that
array.

The future loader owns copied storage and parser allocation policy in TASK 06.B. This PR
does not define owned parsed-plan storage.

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
- unknown field policy is decided by the parser/validator task;
- bundle/source-map hash verification is a later validation task.

## Diagnostics

TASK 06.A returns `SlStatus` from the small handler lookup helper only. It does not emit
plan diagnostics.

Future plan errors should identify section, offending value, and source location when
available.

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

- JSON parser choice and unknown field policy.
- Exact hash verification policy.
- Exact source map required/optional policy.
