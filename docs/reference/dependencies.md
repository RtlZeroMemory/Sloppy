# Dependency Policy

Sloppy uses dependencies as implementation tools, not as product-definition
shortcuts.

## Boundary Rule

Dependencies can provide parser/runtime primitives, but Sloppy still owns:

- artifact contract and plan validation rules;
- lifecycle and diagnostics behavior;
- provider capability policy;
- packaging status and limits.

## Dependencies Visible In Current Source

- `yyjson` is used by `src/core/plan_parse.c` for strict plan parsing.
- `sqlite3`, `libpq`, and ODBC back native provider modules in `src/data/*.c`.
- V8 is isolated under `src/engine/v8/*` and enabled only by explicit build
  mode.
- vcpkg and toolchain resolution are enforced through
  `tools/windows/dev.ps1`/doctor/configure workflow.

## What Dependencies Must Not Do

Do not outsource these to framework or runtime dependencies:

- app lifecycle;
- route graph semantics;
- middleware model;
- dependency-injection rules;
- permissions model;
- diagnostics style;
- resource lifetime and ownership;
- public API design;
- package-manager behavior;
- Node runtime behavior.

## Validation Rule

Dependency availability and feature behavior are separate validation questions.

- A dependency can be installed while a feature lane is still unvalidated.
- Missing optional dependencies should not be reported as passed feature lanes.
- Package-time dependency bundling is not equivalent to runtime compatibility
  status.
