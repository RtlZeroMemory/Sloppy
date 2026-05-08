# Sloppy Plan Golden Fixtures

These fixtures define the documented minimal Sloppy Plan v1 parser contract. Parser tests
read the checked-in files directly through CTest with the repository root as the working
directory, so fixture changes are reviewed as source-controlled test contract changes.

| Fixture | Expected | Diagnostic | Covered by tests | Purpose |
| --- | --- | --- | --- | --- |
| `valid-minimal.plan.json` | success | `SLOPPY_NONE` | yes | Minimal Plan v1 with one handler. |
| `valid-multiple-handlers.plan.json` | success | `SLOPPY_NONE` | yes | Valid handler table with more than one dispatch ID. |
| `valid-route-section.plan.json` | success | `SLOPPY_NONE` | yes | Valid alpha `routes` metadata with one GET route. |
| `valid-route-methods.plan.json` | success | `SLOPPY_NONE` | yes | Valid alpha `routes` metadata for GET, POST, PUT, PATCH, and DELETE. |
| `valid-provider-section.plan.json` | success | `SLOPPY_NONE` | yes | Valid minimal `dataProviders` metadata. |
| `valid-capability-section.plan.json` | success | `SLOPPY_NONE` | yes | Valid minimal `capabilities` metadata tied to a provider token. |
| `valid-capability-skeletons.plan.json` | success | `SLOPPY_NONE` | yes | Valid filesystem and network skeleton capability metadata. |
| `valid-network-required-feature.plan.json` | success | `SLOPPY_NONE` | yes | Valid Plan metadata requiring the CORE-NET-01 `stdlib.net` feature. |
| `valid-filesystem-capability-accesses.plan.json` | success | `SLOPPY_NONE` | yes | Valid CORE-FS-01 filesystem capability access vocabulary. |
| `valid-os-capability-accesses.plan.json` | success | `SLOPPY_NONE` | yes | Valid CORE-OS-01 OS, environment, process, and shutdown signal capability access vocabulary. |
| `unknown-future-field.plan.json` | success | `SLOPPY_NONE` | yes | Unknown top-level and nested fields are ignored in Plan v1. |
| `malformed-json.plan.json` | failure | `SLOPPY_E_MALFORMED_JSON` | yes | Invalid JSON bytes produce a diagnostic instead of a crash. |
| `invalid-version.plan.json` | failure | `SLOPPY_E_INVALID_PLAN_VERSION` | yes | Unsupported `schemaVersion` is rejected. |
| `unknown-required-feature.plan.json` | success | `SLOPPY_NONE` | yes | Parser stores `requiredFeatures`; runtime feature activation rejects unknown entries later. |
| `required-features-not-array.plan.json` | failure | `SLOPPY_E_INVALID_PLAN_FIELD` | yes | `requiredFeatures` must be a JSON array when present. |
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
| `invalid-route-method.plan.json` | failure | `SLOPPY_E_INVALID_PLAN_FIELD` | yes | Plan v1 route metadata rejects methods outside GET, POST, PUT, PATCH, and DELETE. |
| `invalid-route-pattern.plan.json` | failure | `SLOPPY_E_INVALID_ROUTE_PATTERN` | yes | Route patterns must use the supported native alpha syntax. |
| `missing-route-handler.plan.json` | failure | `SLOPPY_E_INVALID_PLAN_FIELD` | yes | Route `handlerId` values must reference `handlers[].id`. |
| `duplicate-route.plan.json` | failure | `SLOPPY_E_INVALID_PLAN_FIELD` | yes | Route `method` and `pattern` pairs must be unique. |
| `duplicate-route-name.plan.json` | failure | `SLOPPY_E_INVALID_PLAN_FIELD` | yes | Non-empty route names must be unique. |
| `invalid-provider-kind.plan.json` | failure | `SLOPPY_E_INVALID_PLAN_FIELD` | yes | Provider values are limited to `sqlite`, `postgres`, and `sqlserver`. |
| `duplicate-provider-token.plan.json` | failure | `SLOPPY_E_INVALID_PLAN_FIELD` | yes | Data provider tokens must be unique. |
| `invalid-capability-kind.plan.json` | failure | `SLOPPY_E_INVALID_PLAN_FIELD` | yes | Capability kinds are limited to the documented database, filesystem, network, queue, OS, environment, process, and signal categories. |
| `invalid-capability-access.plan.json` | failure | `SLOPPY_E_INVALID_PLAN_FIELD` | yes | Capability access must match the capability kind. |
| `missing-capability-token.plan.json` | failure | `SLOPPY_E_INVALID_PLAN_FIELD` | yes | Capability entries require a token. |
| `missing-capability-provider.plan.json` | failure | `SLOPPY_E_INVALID_PLAN_FIELD` | yes | Database capabilities require a provider reference. |
| `non-database-capability-provider.plan.json` | failure | `SLOPPY_E_INVALID_PLAN_FIELD` | yes | Filesystem and network skeleton capabilities cannot reference data providers. |
| `secret-bearing-provider-field.plan.json` | failure | `SLOPPY_E_INVALID_PLAN_FIELD` | yes | Provider/capability plan metadata must not contain raw secret-bearing fields. |
| `duplicate-capability-token.plan.json` | failure | `SLOPPY_E_INVALID_PLAN_FIELD` | yes | Capability tokens must be unique. |

## Conventions

- Valid fixtures use the `valid-*.plan.json` prefix.
- Invalid fixtures name the rejected condition directly, such as
  `missing-bundle-path.plan.json`.
- Fixtures should stay minimal and avoid future service, module, package-manager, V8, or
  HTTP behavior until those sections are implemented.
- When adding a fixture, update this README and the parser fixture matrix in
  `tests/unit/core/test_plan_parse.c` in the same change.
