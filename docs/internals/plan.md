# Plan Internals

## Where It Lives

- `include/sloppy/plan.h`
- `src/core/plan_parse.c`
- `tests/golden/plan/**`
- `tests/cmake/check_source_input_run.cmake`

## Lifecycle

The Plan is parsed and validated before runtime use. Parser failures roll back
arena state and return diagnostics rather than partially accepted structures.

## Invariants

- Route metadata and handler IDs must be deterministic.
- Source map links must match emitted artifacts.
- Feature and provider requirements must be explicit metadata.
