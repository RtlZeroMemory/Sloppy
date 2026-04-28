# Contributing to Sloppy

Sloppy is in the foundation/spec phase. The current repository is meant to lock architecture,
standards, tooling, and quality gates before implementation Phase 1 starts.

No feature work should land before the supporting standards, tests, diagnostics, and tooling
exist. MVP means narrow, not bad. Sloppy is an experiment in disciplined AI-assisted systems
engineering: AI assistance is expected, but human engineering judgment is required.

Do not commit build outputs, generated local artifacts, V8 SDKs, Rust `target` directories,
local dependency caches, release archives, or local binaries.

Windows x64 with `clang-cl`, `lld-link`, CMake, and Ninja is the first-class runtime
developer path. Sloppy is still cross-platform by design.

No WinAPI, POSIX, Linux, or macOS API calls may be added outside `src/platform/*`. Core
modules must not include OS-specific headers. Any new platform-specific behavior must update
`docs/platform-abstraction.md`.

## Local Commands

Run from a Visual Studio Developer PowerShell/Command Prompt or an equivalent initialized
environment that exposes MSVC and Windows SDK `INCLUDE` and `LIB` paths.

```powershell
.\tools\bootstrap.ps1
.\tools\dev.ps1 configure
.\tools\dev.ps1 build
.\tools\dev.ps1 test
.\tools\dev.ps1 format-check
.\tools\dev.ps1 lint
```

The root scripts are compatibility forwarders to `tools/windows/*.ps1`. Windows-specific
tooling lives under `tools/windows/`; future Unix tooling belongs under `tools/unix/`.

## Definition Of Done

- docs/specs updated where behavior, architecture, or public contracts changed;
- tests added, or a missing test is explicitly justified for foundation-only changes;
- docs freshness decision is explicit: updated, or not needed with reason;
- tests reference intended behavior from source docs/specs;
- `format-check` passes;
- `lint` passes;
- CMake build passes;
- CTest passes;
- `cargo fmt --check`, `cargo clippy -- -D warnings`, and `cargo test` pass for compiler
  changes;
- no ignored/generated artifacts are staged.
- platform-boundary checks pass.


## Project Issue Policy

Implementation PRs should reference a task issue or a task file under `docs/project/tasks/`. If no issue exists, create or update one before implementation unless the change is trivial docs-only cleanup. Medium-sized bounded-context PRs are preferred: one coherent building block, not a swarm of microscopic PRs and not a kitchen-sink change.

## Pull Request Policy

- Keep changes small and focused.
- Map implementation PRs to a `docs/roadmap.md` EPIC and task.
- Prefer bounded-context PRs: one coherent module or foundation slice at a time.
- Do not send kitchen-sink PRs that mix unrelated architecture, tooling, and feature work.
- Sloppy prefers bounded, direct implementations.
- Do not add frameworks, registries, plugin systems, generic abstractions, or public
  extension points unless the task or ADR requires them.
- Comments must explain what/why/how where context matters.
- Do not add AI-noise comments that narrate obvious syntax.
- Public APIs and tricky internals require ownership, lifetime, and rationale comments.
- Stale comments must be fixed in the same PR as the behavior change.
- Follow `docs/documentation-policy.md`: code, tests, and docs move together.
- Public API changes require `docs/public/` updates.
- Module implementation changes require `docs/modules/` updates.
- Architecture changes require an ADR update or a new ADR.
- Each implementation PR must include tests and acceptance criteria, or explicitly explain
  why it is documentation/spec-only.
- Tests must verify documented intended behavior, not accidental current behavior.
- Golden output updates require an explanation of the intended behavior change.
- Do not introduce hidden global mutable state.
- Do not use raw `malloc`/`free` outside allocator modules once allocator modules exist.
- Do not leak V8 types outside `src/engine/v8/`.
- Do not expose raw native pointers to JavaScript.
- Do not add feature code without a diagnostics and testing plan.
- Do not add Oxc, V8, libuv, llhttp, yyjson, sqlite, libpq, ODBC, or other real runtime
  dependencies before the relevant implementation phase.
- Put platform-specific tooling under the correct `tools/<platform>/` directory.

## AI/Codex Workflow

AI-assisted development is expected. Use a deliberate loop:

1. architect prompt: turn the roadmap task into files, constraints, tests, and acceptance;
2. dev prompt: implement only the bounded slice;
3. independent reviewer prompt: check spec compliance, C safety, tests, and tooling;
4. fixer prompt: address actionable findings only;
5. final verification prompt: run gates and summarize residual risk.

The docs and ADRs are the source of truth. If an AI-generated implementation needs to guess
architectural intent, update the spec first.

Testing follows `docs/testing-strategy.md`: tests encode documented intent. Do not update
expected outputs merely to match current code without explaining the intent change.

Agent harness expectations:

- consult `AGENTS.md` before editing;
- use the relevant skill docs under `docs/skills/`;
- create or update an execution plan under `docs/exec-plans/` for complex work;
- promote repeated review findings into docs/checks/tools;
- avoid prompt-only rules when a mechanical check is reasonable;
- update `docs/tech-debt-tracker.md` when deferring known cleanup.
