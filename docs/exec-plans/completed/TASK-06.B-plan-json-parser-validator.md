# Execution Plan: TASK 06.B Plan JSON Parser / Validator

## Goal

Implement the minimal Sloppy Plan v1 JSON parser and validator for handwritten
`app.plan.json` bytes.

## Source Docs

- AGENTS.md
- CONTRIBUTING.md
- docs/roadmap.md
- docs/architecture.md
- docs/c-standards.md
- docs/c-style.md
- docs/platform-abstraction.md
- docs/memory.md
- docs/diagnostics.md
- docs/execution-model.md
- docs/concurrency.md
- docs/app-plan.md
- docs/compiler.md
- docs/testing.md
- docs/testing-strategy.md
- docs/quality-gates.md
- docs/documentation-policy.md
- docs/dependencies.md
- docs/review-playbook.md
- docs/modules/plan/README.md
- docs/modules/diagnostics/README.md
- docs/modules/memory/README.md
- docs/skills/c-safety-skill.md
- docs/skills/development-skill.md
- docs/skills/diagnostics-skill.md
- GitHub Issue #38
- GitHub EPIC #7

## Non-goals

- No file-based plan loading.
- No V8, JavaScript loading, or handler execution.
- No compiler extraction or `sloppyc` changes.
- No routes, services, modules, data providers, HTTP, or event loop work.
- No hash verification, source map parsing, JSON serialization, schema registry, or streaming parser.

## Scope

- Add the approved JSON dependency for this phase.
- Add parser API from caller-provided JSON bytes into `SlPlan`.
- Copy parsed strings and handler arrays into the caller-provided arena.
- Validate the minimal Plan v1 fields and handler table.
- Emit basic diagnostics for malformed and invalid plans.
- Add unit tests and fixture coverage.
- Update docs to reflect actual behavior and deferred work.

## Steps

1. Read source docs and issues.
2. Wire `yyjson` through vcpkg/CMake.
3. Add public parser API and diagnostic codes.
4. Implement the parser/validator with arena ownership.
5. Add focused parser tests.
6. Update docs and tech-debt tracking.
7. Run available quality gates.
8. Move this plan to completed with completion notes.

## Acceptance Criteria

- `sl_plan_parse_json` parses `tests/golden/plan/minimal-valid.plan.json`.
- Missing required fields, wrong types, unsupported version, malformed JSON, duplicate IDs,
  invalid IDs, and empty exports fail with diagnostics.
- Parsed strings and handler arrays do not borrow from the yyjson document.
- No OS, V8, compiler, HTTP, route, service, module, or provider scope is added.
- Docs describe parser API, ownership, validation rules, unknown field policy, diagnostics,
  tests, and deferred work.

## Validation Commands

- .\tools\windows\bootstrap.ps1
- .\tools\windows\dev.ps1 configure
- .\tools\windows\dev.ps1 build
- .\tools\windows\dev.ps1 test
- .\tools\windows\dev.ps1 format-check
- .\tools\windows\dev.ps1 lint
- cargo fmt --manifest-path compiler/Cargo.toml -- --check
- cargo clippy --manifest-path compiler/Cargo.toml -- -D warnings
- cargo test --manifest-path compiler/Cargo.toml
- git status --short --ignored

## Decision Log

- Use `yyjson` because `docs/dependencies.md` names it as the intended C JSON/config/plan
  parser and TASK 06.B is the parser phase.
- Allow unknown fields in v1 and ignore them, while rejecting known fields with wrong shape.
- Require the minimal TASK 06.A sections and copied string fields, including bundle and
  source map metadata. Hash values are parsed as required metadata but not cryptographically
  verified in this task.

## Progress Log

- Created branch from fresh `origin/main`.
- Read governing source docs, Issue #38, and EPIC #7.
- Added `yyjson` through vcpkg manifest mode and CMake.
- Installed local `.sdeps/vcpkg` because vcpkg was missing from the environment.
- Added `sl_plan_parse_json`, parser diagnostics, and focused CTest coverage.
- Updated plan, dependency, testing, quality, and tech-debt docs.
- Ran bootstrap, configure, build, test, format-check, lint, and direct cargo gates.

## Risks

- Local vcpkg/toolchain setup may block configure until `yyjson` is installed by manifest mode.
- Diagnostic source locations are intentionally coarse because JSON pointer/source-frame
  support is out of scope.

## Completion Notes

TASK 06.B is implemented as a bounded parser/validator slice. It parses caller-provided JSON
bytes, validates the minimal Plan v1 shape, copies returned plan data into the supplied
arena, emits basic diagnostics, and deliberately defers file loading, compatibility checks,
hash verification, source frames, future schema sections, V8, compiler, HTTP, and execution
work.
