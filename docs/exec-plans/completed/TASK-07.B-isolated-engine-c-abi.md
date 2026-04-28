# Execution Plan: TASK 07.B Isolated Engine C ABI

## Goal

Define the engine-neutral C ABI that lets the C runtime target a future V8 bridge without
exposing V8 or C++ types outside `src/engine/v8/`.

## Source Docs

- `AGENTS.md`
- `CONTRIBUTING.md`
- `docs/project/tasks/EPIC-07/TASK-07.B-isolated-engine-c-abi.md`
- `docs/architecture.md`
- `docs/execution-model.md`
- `docs/concurrency.md`
- `docs/app-plan.md`
- `docs/compiler.md`
- `docs/dependencies.md`
- `docs/build-and-distribution.md`
- `docs/c-standards.md`
- `docs/c-style.md`
- `docs/platform-abstraction.md`
- `docs/diagnostics.md`
- `docs/memory.md`
- `docs/testing-strategy.md`
- `docs/testing.md`
- `docs/quality-gates.md`
- `docs/modules/engine-v8/README.md`
- `docs/modules/plan/README.md`
- `docs/review-playbook.md`
- `docs/skills/c-safety-skill.md`
- `docs/skills/build-tooling-skill.md`
- `docs/skills/development-skill.md`
- `docs/roadmap.md`
- GitHub Issue #41
- GitHub Issue #8

## Non-goals

- No V8 initialization.
- No isolate, context, module loading, JavaScript evaluation, or handler execution.
- No HTTP, event loop, compiler extraction, package-manager, or platform OS work.
- No dynamic engine/plugin registry.

## Scope

- Public `include/sloppy/engine.h` ABI with opaque `SlEngine`.
- Engine options/info structs and explicit engine kind values.
- Create/destroy/info lifecycle functions.
- Minimal handler-call/result structs and function shape.
- Noop engine implementation with unsupported V8 and handler-call behavior.
- CMake and CTest wiring.
- Docs for the actual ABI state and deferred bridge work.

## Steps

1. Refresh from `origin/main` and branch from fresh `origin/main`.
2. Read source docs, issue #41, and EPIC-07 issue #8.
3. Add the engine-neutral header and noop/stub implementation.
4. Add unit tests for lifecycle, unsupported behavior, diagnostics, and invalid arguments.
5. Wire sources/tests into CMake.
6. Update architecture, execution, concurrency, module, diagnostic, score, and debt docs.
7. Run available quality gates.
8. Commit, push, and open the ready PR.

## Decision Log

- Include the handler-call ABI now so TASK 07.C has a stable C-side target, but keep it
  unsupported for the noop engine.
- Allocate the noop engine from caller-provided `SlArena` because it owns no external
  resources in TASK 07.B.
- Do not add V8 placeholder C++ files because the task does not need bridge compilation or
  V8 headers.
- Add `SLOPPY_E_UNSUPPORTED_ENGINE` so unsupported engine behavior has a stable diagnostic.

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

## Completion Notes

- `.\tools\windows\bootstrap.ps1`: passed.
- `.\tools\windows\dev.ps1 configure`: passed and printed `V8 bridge: disabled`.
- `.\tools\windows\dev.ps1 build`: passed. An earlier build reran CMake after the new
  files were added; the final build was clean.
- `.\tools\windows\dev.ps1 test`: passed, 17/17 CTest tests and 4 cargo tests.
- `.\tools\windows\dev.ps1 format-check`: passed.
- `.\tools\windows\dev.ps1 lint`: passed. An earlier lint run failed on an invalid enum
  cast in the new test; the test was corrected and lint passed afterward.
- `cargo fmt --manifest-path compiler/Cargo.toml -- --check`: passed.
- `cargo clippy --manifest-path compiler/Cargo.toml -- -D warnings`: passed.
- `cargo test --manifest-path compiler/Cargo.toml`: passed, 4 tests.
- `git status --short --ignored`: inspected; ignored local build/dependency directories
  were present and not staged.
