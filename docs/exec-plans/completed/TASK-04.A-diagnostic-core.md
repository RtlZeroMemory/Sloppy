# Execution Plan: TASK 04.A Diagnostic Core

## Goal

Implement Sloppy's first structured diagnostic core as one bounded diagnostics foundation
slice.

## Source Docs

- `AGENTS.md`
- `CONTRIBUTING.md`
- `docs/project/tasks/EPIC-04/TASK-04.A-diagnostic-core.md`
- GitHub Issue #33
- `docs/architecture.md`
- `docs/execution-model.md`
- `docs/concurrency.md`
- `docs/app-plan.md`
- `docs/compiler.md`
- `docs/developer-ergonomics.md`
- `docs/modularity.md`
- `docs/data-providers.md`
- `docs/c-standards.md`
- `docs/c-style.md`
- `docs/diagnostics.md`
- `docs/testing-strategy.md`
- `docs/testing.md`
- `docs/quality-gates.md`
- `docs/documentation-policy.md`
- `docs/memory.md`
- `docs/platform-abstraction.md`
- `docs/review-playbook.md`
- `docs/skills/diagnostics-skill.md`
- `docs/skills/c-safety-skill.md`
- `docs/skills/development-skill.md`
- `docs/modules/diagnostics/README.md`
- `docs/modules/core/README.md`
- `docs/modules/memory/README.md`
- `docs/roadmap.md`

## Non-goals

- No source map parser.
- No compiler extraction.
- No plan loader, service container, permission system, or runtime integration.
- No JSON diagnostics.
- No localization.
- No terminal color/styling.
- No source-frame renderer beyond path, line, column, and optional length.
- No public string builder API.

## Scope

- Add diagnostic severity and stable code enums.
- Add user/app source span, related span, hint, and diagnostic object model.
- Add an arena-copying diagnostic builder.
- Add deterministic plain-text rendering.
- Add C unit and golden/snapshot coverage for foundation diagnostics.
- Update diagnostics/testing/module docs and deferred-work trackers.

## Steps

- Read source docs, Issue #33, and existing core/arena primitives.
- Implement `include/sloppy/diagnostics.h` and `src/core/diagnostics.c`.
- Add snapshot fixtures and CTest coverage.
- Update CMake and module/spec docs.
- Run available quality gates.
- Commit, push, and open a draft PR.

## Acceptance Criteria

- Severity enum and stable codes exist and are tested.
- Source spans, related spans, and hints are bounded and tested.
- Builder uses `SlArena` and documents arena lifetime.
- Renderer output is deterministic and covered by at least two snapshots.
- Redaction placeholder behavior is documented and tested.
- No platform, V8, package-manager, source-map, compiler, plan, service, or runtime scope
  creep appears.

## Progress Log

- Created branch `feature/04a-diagnostic-core` from fresh `origin/main`.
- Read required source docs, task file, and GitHub Issue #33.
- Added diagnostics public header, core implementation, CMake wiring, unit tests, and
  golden snapshot reference fixtures.
- Updated diagnostics, testing, core/module, quality score, and tech-debt docs.
- Fixed clang-format and clang-tidy findings, including splitting renderer length
  calculation helpers.
- Addressed PR review feedback by rolling back arena usage on failed related-span insert,
  rejecting uninitialized builders in `finish()`, and wiring snapshot tests to read the
  checked-in fixtures.

## Decision Log

- Diagnostic codes use a small enum plus stable string mapping for v1.
- Builder APIs copy all diagnostic strings and span paths into a caller-provided `SlArena`.
- `SlSourceSpan` is a user/app source span and is documented as distinct from C
  `SlSourceLoc`.
- Related spans and hints use fixed arrays with `SL_DIAG_MAX_RELATED` and
  `SL_DIAG_MAX_HINTS`.
- The text renderer is intentionally plain text, colorless, and format-unstable until a
  public CLI contract exists.
- Redaction is a caller-selected placeholder via `sl_diag_redacted()`; no scanner or policy
  engine is implemented.

## Validation Commands

- `.\tools\windows\bootstrap.ps1`: passed with a warning that the plain shell lacks a
  complete MSVC/Windows SDK build environment.
- `.\tools\windows\dev.ps1 configure`: passed in the plain shell.
- `.\tools\windows\dev.ps1 build`: initially failed in the plain shell because `LIB` did
  not include Windows SDK/MSVC libraries; passed after setting MSVC and Windows Kit
  `INCLUDE`/`LIB` paths explicitly.
- `.\tools\windows\dev.ps1 test`: passed after setting the same `INCLUDE`/`LIB` paths.
- `.\tools\windows\dev.ps1 format-check`: initially failed before formatting
  `test_diagnostics.c`; passed after formatting.
- `.\tools\windows\dev.ps1 lint`: initially failed on renderer function size and enum
  out-of-range test casts; passed after splitting helpers and removing artificial enum
  casts.
- `cargo fmt --manifest-path compiler/Cargo.toml -- --check`: passed.
- `cargo clippy --manifest-path compiler/Cargo.toml -- -D warnings`: passed.
- `cargo test --manifest-path compiler/Cargo.toml`: passed.
- `git status --short --ignored`: run; ignored build/IDE/compiler target artifacts are
  present but unstaged.

## Risks

- The first renderer format should remain simple enough to change before public CLI output
  is declared.
- Arena-backed diagnostic strings must not be used beyond the arena lifetime.

## Completion Notes

TASK 04.A is implemented and validated locally. The only environment caveat is that this
machine's plain shell does not expose Windows SDK/MSVC library paths, so the passing C
build/test/format/lint gates used explicit `INCLUDE` and `LIB` values.
