# Sloppy Plan Golden Fixtures

These fixtures define the documented minimal Sloppy Plan v1 parser contract. Parser tests
read the checked-in files directly through CTest with the repository root as the working
directory, so fixture changes are reviewed as source-controlled test contract changes.

| Fixture | Expected | Diagnostic | Covered by tests | Purpose |
| --- | --- | --- | --- | --- |
| `valid-minimal.plan.json` | success | `SLOPPY_NONE` | yes | Minimal Plan v1 with one handler. |
| `valid-multiple-handlers.plan.json` | success | `SLOPPY_NONE` | yes | Valid handler table with more than one dispatch ID. |
| `unknown-future-field.plan.json` | success | `SLOPPY_NONE` | yes | Unknown top-level and nested fields are ignored in Plan v1. |
| `malformed-json.plan.json` | failure | `SLOPPY_E_MALFORMED_JSON` | yes | Invalid JSON bytes produce a diagnostic instead of a crash. |
| `invalid-version.plan.json` | failure | `SLOPPY_E_INVALID_PLAN_VERSION` | yes | Unsupported `schemaVersion` is rejected. |
| `missing-runtime-minimum-version.plan.json` | failure | `SLOPPY_E_INVALID_PLAN_FIELD` | yes | Required top-level string fields must be present. |
| `missing-bundle.plan.json` | failure | `SLOPPY_E_INVALID_PLAN_FIELD` | yes | The required `bundle` section must be present. |
| `missing-bundle-path.plan.json` | failure | `SLOPPY_E_INVALID_PLAN_FIELD` | yes | Required bundle string fields must be present. |
| `missing-source-map.plan.json` | failure | `SLOPPY_E_INVALID_PLAN_FIELD` | yes | The required `sourceMap` section must be present. |
| `missing-handlers.plan.json` | failure | `SLOPPY_E_INVALID_PLAN_FIELD` | yes | The required `handlers` array must be present. |
| `empty-handlers.plan.json` | failure | `SLOPPY_E_INVALID_PLAN_FIELD` | yes | Minimal Plan v1 requires at least one handler. |
| `invalid-handler-id.plan.json` | failure | `SLOPPY_E_INVALID_PLAN_FIELD` | yes | Handler ID `0` is reserved and invalid. |
| `duplicate-handler-id.plan.json` | failure | `SLOPPY_E_DUPLICATE_HANDLER_ID` | yes | Handler IDs must be unique. |
| `missing-handler-export.plan.json` | failure | `SLOPPY_E_INVALID_PLAN_FIELD` | yes | Each handler requires an `exportName`. |
| `empty-handler-export.plan.json` | failure | `SLOPPY_E_INVALID_PLAN_FIELD` | yes | Handler `exportName` must be non-empty. |
| `wrong-field-type.plan.json` | failure | `SLOPPY_E_INVALID_PLAN_FIELD` | yes | Known fields with the wrong JSON type fail validation. |

## Conventions

- Valid fixtures use the `valid-*.plan.json` prefix.
- Invalid fixtures name the rejected condition directly, such as
  `missing-bundle-path.plan.json`.
- Fixtures should stay minimal and avoid future route, service, module, data-provider,
  package-manager, V8, or HTTP behavior until those sections are implemented.
- When adding a fixture, update this README and the parser fixture matrix in
  `tests/unit/core/test_plan_parse.c` in the same change.
