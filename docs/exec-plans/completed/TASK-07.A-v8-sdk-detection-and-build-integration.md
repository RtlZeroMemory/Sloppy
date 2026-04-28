# Execution Plan: TASK 07.A V8 SDK Detection And Build Integration

## Goal

Add phase-gated V8 SDK detection and build integration without making normal non-V8
foundation builds require V8.

## Source Docs

- `AGENTS.md`
- `CONTRIBUTING.md`
- `docs/project/tasks/EPIC-07/TASK-07.A-v8-sdk-detection-and-build-integration.md`
- `docs/project/epics/EPIC-07-v8-bridge-smoke.md`
- `docs/architecture.md`
- `docs/execution-model.md`
- `docs/concurrency.md`
- `docs/platform-abstraction.md`
- `docs/dependencies.md`
- `docs/build-and-distribution.md`
- `docs/c-standards.md`
- `docs/c-style.md`
- `docs/quality-gates.md`
- `docs/testing-strategy.md`
- `docs/testing.md`
- `docs/agent-harness.md`
- `docs/review-playbook.md`
- `docs/skills/build-tooling-skill.md`
- `docs/skills/platform-boundary-skill.md`
- `docs/skills/c-safety-skill.md`
- `docs/modules/engine-v8/README.md`
- `docs/roadmap.md`
- GitHub Issue #40
- GitHub Issue #8

## Non-goals

- No V8 initialization.
- No isolate, context, module, handler, or JavaScript execution.
- No V8 API calls.
- No V8 headers outside `src/engine/v8/`.
- No SDK download, SDK binaries, or source-build implementation.

## Scope

- CMake options for explicit V8 enablement.
- `SLOPPY_V8_ROOT` validation and imported SDK target when enabled.
- Windows helper script validation mode for the expected SDK layout.
- Documentation updates for V8 SDK layout, optional status, and follow-up debt.

## Steps

1. Read source docs and current build/tooling files.
2. Add CMake V8 gate and SDK layout validation.
3. Update `tools/windows/fetch-v8.ps1` to validate a caller-provided SDK root.
4. Update V8 module and dependency/build/quality docs.
5. Run default non-V8 configure/build/test and negative V8 configure/script checks.
6. Run format/lint/cargo gates as available.
7. Commit, push, and open the ready PR.

## Acceptance Criteria

- Default configure/build does not require V8.
- V8-enabled configure fails clearly when `SLOPPY_V8_ROOT` is missing or invalid.
- Expected V8 SDK layout is documented and checked.
- `src/engine/v8` remains the only V8 implementation boundary.
- CI remains non-V8.

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
- `.\tools\windows\fetch-v8.ps1 -ValidateOnly -V8Root "<missing-path>"`
- isolated V8-enabled configure against a missing SDK path
- isolated V8-enabled configure against a fake SDK layout
- `git status --short --ignored`

## Decision Log

- Use `SLOPPY_ENABLE_V8=ON` as the explicit bridge enablement switch.
- Keep `SLOPPY_ENGINE` as a cache string for plan/build intent, defaulting to `none`;
  `SLOPPY_ENGINE=v8` also enables the V8 gate.
- Treat the exact V8 SDK source/version and source-build workflow as deferred debt.
- Have `tools/windows/dev.ps1 configure` reset to the non-V8 default unless the caller
  explicitly passes a V8-related CMake argument, so older local caches with the previous
  placeholder `SLOPPY_ENGINE=v8` do not break normal foundation builds.

## Progress Log

- Started from fresh `origin/main` on `feature/07a-v8-sdk-detection`.
- Read requested source docs, Issue #40, and EPIC-07 Issue #8.
- Implemented the CMake V8 gate and `Sloppy::V8` imported interface target.
- Added `tools/windows/fetch-v8.ps1 -ValidateOnly` layout validation.
- Updated build, dependency, V8 module, quality, quality score, and debt docs.
- Ran the standard gates and V8 validation checks listed in the completion notes.

## Risks

- V8 library names vary by distribution; the current check validates the documented Windows
  SDK family without pretending the final exact library list is settled.

## Completion Notes

- `.\tools\windows\bootstrap.ps1`: passed.
- `.\tools\windows\dev.ps1 configure`: passed and printed `V8 bridge: disabled`.
- `.\tools\windows\dev.ps1 build`: passed.
- `.\tools\windows\dev.ps1 test`: passed, 16/16 CTest tests and 4 cargo tests.
- `.\tools\windows\dev.ps1 format-check`: passed.
- `.\tools\windows\dev.ps1 lint`: passed.
- `cargo fmt --manifest-path compiler/Cargo.toml -- --check`: passed.
- `cargo clippy --manifest-path compiler/Cargo.toml -- -D warnings`: passed.
- `cargo test --manifest-path compiler/Cargo.toml`: passed, 4 tests.
- `.\tools\windows\fetch-v8.ps1 -ValidateOnly -V8Root V:\Slop\missing-v8-sdk`: failed as
  expected with a missing SDK root diagnostic.
- `.\tools\windows\fetch-v8.ps1 -ValidateOnly -V8Root build\v8-fake-sdk`: passed.
- `.\tools\windows\fetch-v8.ps1 -ValidateOnly -V8Root build\v8-no-core-sdk`: failed as
  expected with `lib/v8.lib or lib/v8_monolith*.lib` missing, proving platform/base
  support libraries cannot satisfy the core V8 library check.
- Isolated V8-enabled CMake configure with `SLOPPY_V8_ROOT=V:\Slop\missing-v8-sdk` failed
  as expected with `V8 bridge: enabled but SLOPPY_V8_ROOT is not a directory`.
- Isolated V8-enabled CMake configure against `build\v8-no-core-sdk` failed as expected
  with `V8 bridge: enabled but no core V8 library was found`.
- Isolated V8-enabled CMake configure against a fake SDK layout under `build/` passed,
  proving the imported target wiring parses. An earlier attempt without importing the MSVC
  environment failed before project configure because the compiler could not link a test
  program.
