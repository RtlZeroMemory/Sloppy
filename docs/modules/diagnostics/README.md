# Diagnostics Module

## Status

Partially implemented for TASK 04.A.

## Purpose

Provide stable, deterministic diagnostics for humans and tools.

## Scope

Implemented now:

- severity enum;
- small enum-backed diagnostic code model;
- user/app source span model;
- bounded related spans and hints;
- plain diagnostic object;
- arena-copying diagnostic builder;
- deterministic plain-text renderer;
- initial golden/snapshot fixtures.

## Non-goals

No source map parser, source-frame renderer, JSON diagnostics, localization, IDE protocol,
terminal colors, diagnostic registry, global sink, plan loader, service container, or
runtime integration in this foundation pass.

## Public/Internal API

Implemented public header:

- `include/sloppy/diagnostics.h`

Implemented API:

- `SlDiagSeverity` and `sl_diag_severity_name`;
- `SlDiagCode` and `sl_diag_code_name`;
- `SlSourceSpan`, `sl_source_span_unknown`, and `sl_source_span_make`;
- `SlDiagRelated` and `SlDiag`;
- `SlDiagBuilder`, `sl_diag_builder_init`, `sl_diag_builder_set_primary_span`,
  `sl_diag_builder_add_related`, `sl_diag_builder_add_hint`, and
  `sl_diag_builder_finish`;
- `sl_diag_render_text`;
- `sl_diag_redacted`.

## Ownership/Lifetime Rules

Standalone `SlStr` values and `SlSourceSpan.path` are borrowed views.

Builder APIs copy diagnostic message text, hint text, related messages, and span paths into
the provided `SlArena`. Finished diagnostics produced by the builder remain valid until
that arena is reset or the caller-owned arena backing buffer lifetime ends.

The renderer returns an arena-owned `SlStr` that is not NUL-terminated and remains valid
until the render arena is reset.

## Invariants

Diagnostic codes are stable public contracts once released. TASK 04.A keeps the initial code
set intentionally small.

Source span line and column are 1-based when present. `SlSourceSpan` represents user/app
source and is distinct from `SlSourceLoc`, which represents C call-site source.

Related spans and hints are bounded by `SL_DIAG_MAX_RELATED` and `SL_DIAG_MAX_HINTS`.

The current text renderer is deterministic, colorless, and does not normalize paths. Its
format is a foundation test contract, not a released CLI output contract.

## Diagnostics

This module defines diagnostic output and tests placeholder redaction behavior. It does not
scan arbitrary strings for secrets; callers must pass `sl_diag_redacted()` or otherwise
avoid including secret values.

## Tests

CTest registers `tests/unit/core/test_diagnostics.c`, covering:

- severity name mapping;
- diagnostic code name mapping;
- unknown/default source spans;
- builder success and invalid arguments;
- related span and hint bounds;
- basic renderer output;
- primary span, related span, and hint rendering through snapshots;
- redacted placeholder rendering.

Initial snapshot expectations are asserted directly in `tests/unit/core/test_diagnostics.c`
to keep the C test dependency-free. Matching reference fixtures live under
`tests/golden/diagnostics/`:

- `missing_service.snap`;
- `invalid_plan_version.snap`.

## Source Docs

- `docs/diagnostics.md`;
- `docs/testing-strategy.md`;
- `docs/testing.md`;
- `docs/memory.md`;
- `docs/c-standards.md`.

## Open Questions

- Exact JSON output shape.
- Source-frame rendering.
- Source-map integration.
- Localization.
- Diagnostic metadata and structured fixes.
- Redaction policy engine.
