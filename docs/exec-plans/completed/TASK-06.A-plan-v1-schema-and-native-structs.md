# Execution Plan: TASK 06.A Plan v1 Schema and Native Structs

## Goal

Define Sloppy Plan v1's minimal native C schema, helper functions, and handwritten
fixture structure without implementing parsing, validation, loading, or execution.

## Source Docs

- `AGENTS.md`
- `CONTRIBUTING.md`
- GitHub Issue #37
- GitHub Issue #7, EPIC-06
- `docs/project/tasks/EPIC-06/TASK-06.A-plan-v1-schema-and-native-structs.md`
- `docs/project/epics/EPIC-06-plan-schema-loader.md`
- `docs/project/README.md`
- `docs/architecture.md`
- `docs/execution-model.md`
- `docs/concurrency.md`
- `docs/app-plan.md`
- `docs/compiler.md`
- `docs/c-standards.md`
- `docs/c-style.md`
- `docs/diagnostics.md`
- `docs/memory.md`
- `docs/testing-strategy.md`
- `docs/testing.md`
- `docs/quality-gates.md`
- `docs/documentation-policy.md`
- `docs/platform-abstraction.md`
- `docs/review-playbook.md`
- `docs/skills/README.md`
- `docs/skills/c-safety-skill.md`
- `docs/skills/development-skill.md`
- `docs/modules/plan/README.md`
- `docs/roadmap.md`
- `docs/quality-score.md`
- `docs/tech-debt-tracker.md`

## Non-goals

- No JSON parser or JSON validation.
- No app.plan loader or file I/O.
- No route, service, module, data provider, permission, or validation model.
- No source map parser or hash verification.
- No compiler extraction or fake emitter.
- No V8 bridge, handler execution, HTTP/router, event loop, platform OS API, Node, or
  package-manager behavior.

## Scope

- Add `include/sloppy/plan.h` with minimal borrowed native structs and ownership comments.
- Add `src/core/plan.c` helper implementation.
- Add C unit coverage for version support, handler ID rules, handler lookup, duplicate ID
  detection, null handling, empty handler tables, and fixture availability.
- Add valid and invalid handwritten fixtures under `tests/golden/plan/`.
- Update module/spec/testing docs and deferred decision trackers to match the implemented
  shape.

## Steps

- Refresh and branch from fresh `origin/main`.
- Read the required source docs and issue context.
- Implement the narrow C API and test wiring.
- Add golden fixtures with field names aligned to docs.
- Update docs to distinguish implemented schema structs/fixtures from future loader work.
- Run available quality gates.
- Commit, push, and open a normal PR.

## Acceptance Criteria

- `include/sloppy/plan.h` exists and documents ownership/lifetime.
- Minimal Plan v1 native structs exist.
- Handler ID type, invalid/reserved behavior, version constants, lookup, and duplicate
  detection are implemented and tested.
- Fixtures exist under `tests/golden/plan/`.
- No parser, loader, route/service/module/data provider, compiler, V8, HTTP, or platform
  OS implementation lands.
- Docs reflect actual implementation and deferred follow-ups.

## Validation Commands

- `.\tools\windows\bootstrap.ps1`: passed.
- `.\tools\windows\dev.ps1 configure`: passed.
- `.\tools\windows\dev.ps1 build`: passed.
- `.\tools\windows\dev.ps1 test`: passed, including `core.plan.contract`.
- `.\tools\windows\dev.ps1 format-check`: passed.
- `.\tools\windows\dev.ps1 lint`: passed.
- `cargo fmt --manifest-path compiler/Cargo.toml -- --check`: passed.
- `cargo clippy --manifest-path compiler/Cargo.toml -- -D warnings`: passed.
- `cargo test --manifest-path compiler/Cargo.toml`: passed.
- `git status --short --ignored`: run; ignored local `.idea/`, `build/`,
  `cmake-build-debug/`, and `compiler/target/` artifacts are present but unstaged.

## Decision Log

- Minimal JSON fixture field names use `schemaVersion`, `compilerVersion`,
  `runtimeMinimumVersion`, `stdlibVersion`, `target.platform`, `target.engine`,
  `bundle.path/id/hash`, `sourceMap.path/id/hash`, and
  `handlers[].id/exportName/displayName`.
- Native plan structs use borrowed `SlStr` views and a borrowed caller-owned handler array.
  The future TASK 06.B loader owns copied storage and parser lifetimes.
- Handler ID 0 is reserved/invalid. Handler lookup rejects ID 0 and returns
  `SL_STATUS_OUT_OF_RANGE` for valid missing IDs.
- Duplicate handler ID detection is a simple allocation-free table scan. Full malformed
  table diagnostics are deferred to the validator.

## Progress Log

- Created branch `feature/06a-plan-v1-schema` from fresh `origin/main`.
- Read required source docs, GitHub Issue #37, and EPIC-06 issue body.
- Added Plan v1 public structs/helpers, CMake wiring, unit tests, and handwritten fixtures.
- Updated plan, execution, compiler, testing, core/module, quality score, and tech-debt
  docs.
- Added a missing-source-map fixture because the source docs already list that invalid plan
  case.

## Risks

- The biggest risk is scope creep into parsing or validation. Keep this PR to native
  struct shape, fixture intent, and small helper behavior.

## Completion Notes

TASK 06.A is implemented as a bounded schema/struct/fixture PR. No JSON parser, loader,
validator, route/service/module/data provider model, compiler emission, V8, HTTP, platform
OS API, package-manager, or Node compatibility work was added.
