# Execution Plan: TASK 06.C Golden Plan Fixtures

## Goal

Harden Sloppy Plan golden fixtures and parser fixture coverage for Issue #39.

## Source Docs

- AGENTS.md
- CONTRIBUTING.md
- docs/c-standards.md
- docs/c-style.md
- docs/app-plan.md
- docs/execution-model.md
- docs/compiler.md
- docs/diagnostics.md
- docs/testing-strategy.md
- docs/testing.md
- docs/quality-gates.md
- docs/documentation-policy.md
- docs/modules/plan/README.md
- docs/modules/diagnostics/README.md
- docs/roadmap.md
- GitHub Issue #39
- GitHub Issue #7

## Non-goals

- No V8, execution, HTTP, compiler extraction, routes, services, modules, data providers,
  or package-manager behavior.
- No production file loader or runtime compatibility checks.
- No generic fixture framework or schema generator.

## Scope

- Audit and complete `tests/golden/plan/`.
- Document every plan fixture.
- Update parser tests to cover the fixture matrix.
- Update plan/testing docs where fixture behavior and conventions are clarified.

## Steps

1. Read source docs, task issue, epic issue, parser code, tests, fixtures, and CMake wiring.
2. Add missing valid and invalid plan fixtures using consistent names.
3. Add `tests/golden/plan/README.md` with expected outcomes, diagnostics, and coverage.
4. Refactor parser tests around a simple fixture matrix and expected value checks.
5. Update plan/testing docs and tech-debt notes.
6. Run available quality gates and report results honestly.

## Acceptance Criteria

- Fixture directory is documented.
- Valid fixtures parse and expected fields are asserted.
- Invalid fixtures fail with expected diagnostic code and error severity.
- CTest can locate fixtures reliably through the existing source-root working directory.
- No runtime feature scope creep.

## Validation Commands

- `.\tools\windows\bootstrap.ps1`
- `.\tools\windows\dev.ps1 configure`
- `.\tools\windows\dev.ps1 build`
- `.\tools\windows\dev.ps1 test`
- `.\tools\windows\dev.ps1 format-check`
- `.\tools\windows\dev.ps1 lint`
- `cargo fmt --manifest-path compiler/Cargo.toml -- --check`
- `cargo clippy --manifest-path compiler/Cargo.toml -- -D warnings`
- `cargo test --manifest-path compiler/Cargo.toml`
- `git status --short --ignored`

## Decision Log

- Preserve the existing CTest source-root working directory approach for fixture lookup
  because it is simple, already present, and avoids machine-local absolute paths.
- Use a human-readable fixture README instead of a JSON manifest because the primary need is
  review clarity.

## Progress Log

- Read required source docs, Issue #39, EPIC-06 Issue #7, existing parser, tests, fixtures,
  and CMake wiring.
- Confirmed parser behavior already supports the requested fixture matrix without production
  feature additions.
- Added a human-readable fixture README and completed valid/invalid Plan v1 fixtures.
- Updated parser tests to read the checked-in fixture matrix and verify diagnostic
  code/severity plus expected valid fixture values.
- Updated plan/testing docs, quality score, and tech debt notes.
- Ran the requested Windows and Rust quality gates successfully.

## Risks

- Fixture naming churn can break tests if references are missed.
- Adding too much harness structure would exceed the bounded testing/docs scope.

## Completion Notes

Completed as a bounded testing/docs PR. Production parser behavior was not expanded beyond
the already documented minimal Plan v1 validation behavior.
