# Sloppy Agent Guide

## Mission

Sloppy is an AI-assisted TypeScript backend application runtime/app-host.

It has a C runtime kernel, with V8 reached later through an isolated C++ bridge. The Rust
`sloppyc` project is a placeholder today and may become an Oxc-based tool later.

The developer loop is Windows-first and cross-platform by design. Developer ergonomics is
the product wedge: Sloppy should feel designed, not assembled from runtime primitives and
framework soup.

Clean/safe C is non-negotiable. The name is a joke; the standards are not.

## How to work

- Read this file before changing files.
- Read the relevant docs before implementing.
- Keep changes bounded to the requested roadmap slice.
- Create or update execution plans for complex multi-step work.
- Update docs/ADRs when architecture changes.
- Add or update tests/checks where applicable.
- Keep code, tests, and docs moving together.
- Run available checks before claiming a task is done.
- Report commands honestly, including commands not run.
- Never claim success for a command you did not run.
- Avoid future-phase implementation unless explicitly asked.
- Prefer mechanical checks over reviewer memory.
- Promote repeated review feedback into docs/checks/tools.
- Comment rationale/invariants where needed; do not narrate obvious syntax.
- Track deferred cleanup in `docs/tech-debt-tracker.md`.
- If docs conflict, pause and resolve the source of truth first.
- If a task is spec-only, say why tests did not change.
- Before changing code, identify whether user-facing docs, module docs, architecture docs,
  or ADRs need updates.
- Tests must verify documented intent, not current accidental behavior.

## Source-of-truth map

Project management:

- [GitHub project model](docs/project/README.md)

Implementation work should map to a project task under `docs/project/tasks/` unless it is trivial docs-only cleanup.

Core architecture:

- [Architecture](docs/architecture.md)
- [Execution model](docs/execution-model.md)
- [Concurrency and async model](docs/concurrency.md)
- [Developer ergonomics](docs/developer-ergonomics.md)
- [Platform abstraction](docs/platform-abstraction.md)

Runtime standards:

- [C style](docs/c-style.md)
- [C standards](docs/c-standards.md)
- [JavaScript/TypeScript standards](docs/js-ts-standards.md)
- [Rust standards](docs/rust-standards.md)
- [Memory](docs/memory.md)
- [Diagnostics](docs/diagnostics.md)
- [Documentation policy](docs/documentation-policy.md)
- [Testing strategy](docs/testing-strategy.md)

System shape:

- [Modularity](docs/modularity.md)
- [Data providers](docs/data-providers.md)
- [Testing](docs/testing.md)
- [Quality gates](docs/quality-gates.md)
- [Roadmap](docs/roadmap.md)

Agent workflow:

- [Agent harness](docs/agent-harness.md)
- [Agent skills](docs/skills/README.md)
- [Execution plans](docs/exec-plans/README.md)

## Language Standards

- C/C++ runtime work: read `docs/c-standards.md` and `docs/c-style.md`.
- JavaScript/TypeScript stdlib, public API, and examples: read `docs/js-ts-standards.md`.
- Rust compiler/tooling: read `docs/rust-standards.md`.
- All languages: follow `docs/testing-strategy.md`, `docs/documentation-policy.md`, and
  `docs/review-playbook.md`.

Language-specific gates are part of `tools/windows/dev.ps1 lint`.

## Hard boundaries

- No OS APIs outside `src/platform/*`.
- No OS headers in core modules.
- No V8 types outside `src/engine/v8/*`.
- No native worker thread may enter a V8 isolate unless it is the owning engine thread or
  the engine bridge explicitly documents that ownership.
- No JS raw native pointers.
- No raw `malloc`/`free` outside allocator modules once allocator exists.
- No package-manager scope.
- No Node compatibility by default.
- No package-manager behavior unless a scoped task and docs require it.
- No Node compatibility assumptions.
- No runtime features before foundation tasks.
- No overengineering in JS/Rust either; keep public API and compiler/tooling work direct.
- Avoid speculative abstraction; simple direct C is preferred unless a documented
  boundary/invariant requires abstraction.
- No generated/build artifacts committed.
- No hidden global mutable runtime state.
- No fake-success placeholders in implemented paths.
- No dependency additions without the relevant phase/docs.

## Common commands

Canonical Windows workflow:

```powershell
.\tools\windows\bootstrap.ps1
.\tools\windows\dev.ps1 configure
.\tools\windows\dev.ps1 build
.\tools\windows\dev.ps1 test
.\tools\windows\dev.ps1 format-check
.\tools\windows\dev.ps1 lint
```

Root wrappers may exist, but `tools/windows` is canonical for the Windows workflow.

Codex sessions on this machine must assume the compatible local V8 SDK is available through
the repo scripts. For implementation PRs that touch runtime, app-host, compiler, bootstrap,
provider, or configuration behavior, run and report a separate V8-enabled Windows lane:

```powershell
.\tools\windows\resolve-v8-sdk.ps1
.\tools\windows\dev.ps1 configure -Preset windows-relwithdebinfo -EnableV8
.\tools\windows\dev.ps1 build -Preset windows-relwithdebinfo
.\tools\windows\dev.ps1 test -Preset windows-relwithdebinfo
```

If SDK resolution fails on this machine, treat that as a local environment blocker and
report it honestly instead of counting the V8 lane as skipped or optional.

## Before opening a PR

- Identify the source docs that govern the change.
- Keep the PR bounded to one coherent task or foundation slice.
- Add/update tests or explain why not.
- Update docs if behavior, architecture, or workflow changes.
- Run checks and report failures honestly.
- Inspect `git status`.
- Review the changed files for accidental scope creep.
- Verify docs and checks agree.
- Ensure no ignored/generated artifacts are staged.
