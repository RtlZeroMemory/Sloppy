# Execution Plan: TASK 03.A SlArena Foundation

## Goal

Implement Sloppy's first caller-backed arena allocation primitive as one bounded memory
foundation slice.

## Source Docs

- `AGENTS.md`
- `CONTRIBUTING.md`
- `docs/project/tasks/EPIC-03/TASK-03.A-slarena-foundation.md`
- GitHub Issue #31
- `docs/c-standards.md`
- `docs/c-style.md`
- `docs/memory.md`
- `docs/testing-strategy.md`
- `docs/testing.md`
- `docs/quality-gates.md`
- `docs/documentation-policy.md`
- `docs/platform-abstraction.md`
- `docs/review-playbook.md`
- `docs/skills/c-safety-skill.md`
- `docs/skills/development-skill.md`
- `docs/modules/memory/README.md`
- `docs/modules/core/README.md`
- `docs/roadmap.md`
- ADR 0006

## Non-goals

- No OS page allocator.
- No `malloc`-backed factory.
- No generic allocator framework.
- No request lifecycle, resource table, V8, runtime, or package-manager work.

## Scope

- Add `SlArena` and `SlArenaMark`.
- Initialize over caller-provided memory.
- Add alignment-aware allocation with checked arithmetic.
- Add mark/reset and full reset.
- Track high-water usage.
- Add debug poisoning under `SL_ENABLE_ASSERTS`.
- Add C unit tests and CMake/CTest wiring.
- Update memory/core docs.

## Steps

- Read source docs and existing TASK 02.A primitives.
- Implement public header and core source.
- Add focused unit coverage.
- Update CMake and docs.
- Run available quality gates.
- Open a bounded PR.

## Acceptance Criteria

- SlArena API exists and documents ownership/lifetime.
- Allocation is alignment-aware and overflow-safe.
- Mark/reset and full reset are tested.
- Stats are tested.
- Invalid argument and capacity failure paths are tested.
- No OS, V8, package-manager, or future runtime scope appears.

## Validation Commands

- `.\tools\windows\bootstrap.ps1`: passed with warning that the default shell lacked a
  complete VS/MSVC SDK environment.
- `.\tools\windows\dev.ps1 configure`: passed in the default shell before the build exposed
  missing standard headers.
- `.\tools\windows\dev.ps1 build`: failed in the default shell because `clang-cl` could not
  find standard headers; passed after initializing VS 2022 and adding SDK/Cargo paths.
- `.\tools\windows\dev.ps1 test`: passed after the same environment initialization.
- `.\tools\windows\dev.ps1 format-check`: passed after the same environment initialization.
- `.\tools\windows\dev.ps1 lint`: passed after the same environment initialization; its
  artifact hygiene subcheck warned that `git` was not visible in that repaired environment.
- `cargo fmt --manifest-path compiler/Cargo.toml -- --check`: passed.
- `cargo clippy --manifest-path compiler/Cargo.toml -- -D warnings`: passed.
- `cargo test --manifest-path compiler/Cargo.toml`: passed.
- `git status --short --ignored`: run from the normal shell to report artifact state.

## Decision Log

- Zero-size allocation is rejected as `SL_STATUS_INVALID_ARGUMENT` to avoid ambiguous
  pointer identity and lifetime for allocations that own no bytes.
- Zero-capacity arenas are valid, including with a NULL backing buffer, but no allocation
  can succeed.
- All builds use a generation counter to reject marks captured before a full reset.
- Debug poisoning is implemented under `SL_ENABLE_ASSERTS` with simple allocation and reset
  byte patterns.
- Debug poisoning uses explicit byte loops instead of `memset` because clang-tidy treats
  `memset` as an insecure API under this repo's warning-as-error lint gate.

## Progress Log

- Created branch `feature/03a-slarena-foundation`.
- Read required source docs, Issue #31, ADR 0006, and existing TASK 02.A primitives.
- Added arena header, implementation, unit tests, and CMake wiring.
- Updated memory/core docs with implemented behavior and non-goals.
- Ran available validation gates and addressed lint findings.

## Risks

- The first arena API must remain narrow enough not to become a premature allocator
  framework.
- Generation tracking is part of the public mark contract so stale marks after full reset
  are rejected in release and assert-enabled builds.

## Completion Notes

TASK 03.A is implemented and validated locally. The remaining environment note is that this
machine's plain shell is not a complete VS developer shell; the Windows SDK include/lib
paths and Cargo path were added explicitly for the passing C build, test, format, and lint
gates.
