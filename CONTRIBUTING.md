# Contributing to Sloppy

Sloppy is a pre-alpha runtime project. The repository is expected to read and behave like a
serious engineering project even while major product tracks remain unfinished.

Do not commit build outputs, generated local artifacts, V8 SDKs, Rust `target`
directories, local dependency caches, release archives, or local binaries.

Windows x64 with `clang-cl`, `lld-link`, CMake, and Ninja is the most complete validated
local runtime developer path today. Sloppy remains cross-platform by design. No WinAPI,
POSIX, Linux, or macOS API calls may be added outside `src/platform/*`; core modules must
not include OS-specific headers.

## Local Commands

Run from a Visual Studio Developer PowerShell/Command Prompt or an equivalent initialized
environment:

```powershell
.\tools\windows\bootstrap.ps1
.\tools\windows\dev.ps1 doctor
.\tools\windows\dev.ps1 configure
.\tools\windows\dev.ps1 build
.\tools\windows\dev.ps1 test
.\tools\windows\dev.ps1 format-check
.\tools\windows\dev.ps1 lint
git diff --check
```

Root scripts are compatibility forwarders to `tools/windows/*.ps1`.

## Definition Of Done

- source docs, ADRs, module docs, or user-facing docs updated when behavior or architecture
  changes;
- tests added or an explicit reason given for docs-only/spec-only changes;
- tests reference intended behavior from source docs/specs;
- positive and negative paths covered where applicable;
- golden updates explain the intended behavior change;
- evidence lanes reported honestly;
- no ignored/generated artifacts staged;
- relevant format, lint, build, and test gates run or reported as not run with reason;
- platform, V8, memory, diagnostics, and JavaScript boundary rules preserved.

## Evidence Lane Report

Every implementation PR must report test evidence by lane and status. Use the lane names
from `docs/testing-strategy.md` and `docs/quality-gates.md`: default non-V8,
compiler/Plan, V8-gated, source-input, package outside-checkout, platform-specific,
dependency-backed, live-network/live-provider, fuzz/property, stress/torture,
advanced static analysis, sanitizer/memory-safety, and benchmark. Status values are
`PASS`, `FAIL`, `SKIPPED`, `UNAVAILABLE`, `DEFERRED`, or `NOT RUN`. Skipped optional gates
are not pass claims.

The report must state:

- expected behavior under test and the source-of-truth contract;
- positive and negative paths covered;
- evidence lanes run with exact commands;
- skipped or unavailable lanes with exact reasons;
- goldens changed and why the new output is intended;
- secret/redaction checks run;
- known deferred coverage with issue references.

Benchmark results are never correctness evidence. V8, package, live-provider,
advanced static analysis, fuzz/property, stress/torture, sanitizer, and platform-specific
results must stay separate from default non-V8 evidence.

## Implementation Contract for Reviewers

Large or high-risk PRs must include an implementation contract that reviewers can check
against. Include:

- source docs and issues read;
- intended behavior;
- explicit non-goals;
- files or surfaces touched;
- docs, tests, and code expected to move together;
- negative paths and diagnostics that must be covered;
- evidence lanes required, skipped, or deferred;
- known residual risk or follow-up.

Reviewers should compare the PR against this contract, not only inspect local code style.

## Project Issue Policy

Implementation PRs should reference a current GitHub issue or a task file under
`docs/project/tasks/`. If no issue exists, create or update one before implementation
unless the change is trivial docs-only cleanup. GitHub owns live issue state; local issue
indexes are snapshots.

## Pull Request Policy

- Prefer bounded, coherent, reviewable PRs.
- One large PR is acceptable when it represents one bounded context.
- Avoid unrelated refactors, broad rewrites, and formatting churn.
- Do not split coherent work into tiny PRs only to satisfy process ceremony.
- Do not mix unrelated architecture, tooling, and feature work.
- Do not add frameworks, registries, plugin systems, generic abstractions, or public
  extension points unless a source doc or ADR requires them.
- Comments must explain rationale, ownership, lifetime, safety, platform, engine, or
  threading invariants where context matters.
- Stale comments must be fixed in the same PR as the behavior change.
- Tests must verify documented intended behavior, not accidental current behavior.
- Do not introduce hidden global mutable state.
- Do not leak V8 types outside `src/engine/v8/`.
- Do not expose raw native pointers to JavaScript.
- Do not add package-manager scope or Node compatibility assumptions.
- Do not add public alpha, production-readiness, performance, package-readiness,
  provider-readiness, or Node/Bun/Deno compatibility claims without source docs and
  matching evidence.

## Agent Workflow

Agent-assisted development is part of the repository workflow, but outcome quality matters
more than prompt choreography. Agents should read `AGENTS.md`, use the relevant skills under
`docs/skills/`, identify the source docs first, keep scope bounded, run checks, and report
commands honestly.

Use targeted independent reviewers or subagents for high-risk work such as C safety, V8
boundaries, concurrency, providers, permissions/security, diagnostics redaction, packaging,
release evidence, and repository-wide documentation cleanup. Trivial changes do not need a
specialist sweep.

Promote repeated review findings into docs, checks, or tools. Track deferred cleanup in
`docs/tech-debt-tracker.md` when the cleanup is real but outside the current PR.
