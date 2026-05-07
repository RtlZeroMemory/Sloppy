# Developer Workflow

## Purpose

This document describes how Sloppy work should be sliced, built, reviewed, and verified.
The goal is to move quickly without lowering the engineering bar.

## Scope

This covers:

- local commands;
- bounded-context PR model;
- agent workflow;
- review modes;
- blocking versus non-blocking findings;
- follow-up issue rules;
- source/review archive hygiene.

## Local Loop

Use a Visual Studio Developer PowerShell/Command Prompt or a normal PowerShell with Visual
Studio C++ tools installed. The Windows scripts import the MSVC/Windows SDK environment
when needed.

```powershell
.\tools\windows\bootstrap.ps1
.\tools\windows\dev.ps1 configure
.\tools\windows\dev.ps1 build
.\tools\windows\dev.ps1 test
.\tools\windows\dev.ps1 format-check
.\tools\windows\dev.ps1 lint
```

Root `tools/*.ps1` scripts are compatibility forwarders to `tools/windows/*.ps1`.

Use `.\tools\windows\dev.ps1 clean` to remove only `build/<preset>`. It does not remove
`compiler/target/` or dependency caches.

## Bounded-Context PR Model

Preferred PR shape:

- one roadmap EPIC;
- one coherent module/foundation slice;
- docs updated with any behavior change;
- tests for the touched slice;
- no kitchen-sink refactors;
- no unrelated formatting churn.

Examples:

- good: "EPIC 02: SlStr/SlBytes views and tests";
- good: "EPIC 04: diagnostic formatter and first snapshots";
- bad: "core primitives, diagnostics, router, and compiler cleanup".

## Agent Workflow

Agent assistance is part of the repository workflow, but the contract is outcome-based:
read the source docs, keep the context bounded, implement only the scoped behavior, report
evidence honestly, and promote repeated review feedback into docs/checks/tools.

Use targeted independent reviewers or subagents for high-risk slices such as C safety, V8
boundaries, concurrency, providers, permissions/security, diagnostics redaction, packaging,
release evidence, and repository-wide documentation cleanup. Trivial changes do not need a
specialist sweep.

## Review Modes

Spec compliance review:

- matches roadmap EPIC/task;
- does not implement out-of-scope runtime features;
- updates docs/ADRs when architecture changes.

C safety/quality review:

- ownership documented;
- cleanup paths are explicit;
- checked arithmetic used for sizes;
- no VLA;
- no raw malloc/free outside allocator modules;
- no OS APIs outside platform implementation directories.

Build/tooling review:

- CMake/CTest updated;
- Cargo gates pass when compiler touched;
- format/lint pass;
- no generated artifacts staged.

Product ergonomics review:

- public API examples remain app-host oriented;
- diagnostics are actionable;
- no low-level primitive path becomes the primary user story.

## Blocking Versus Non-Blocking Findings

Blocking findings:

- correctness bug;
- memory safety risk;
- platform-boundary violation;
- V8 type leak outside `src/engine/v8/`;
- missing required tests for implemented behavior;
- out-of-scope feature implementation;
- tracked generated artifact.

Non-blocking findings:

- naming polish;
- future refactor suggestion;
- optimization without measured need;
- additional docs examples not needed for current acceptance.

Non-blocking findings should become follow-up issues when they are real but not required for
the PR's acceptance criteria.

## Follow-Up Issues

Create a follow-up when:

- the issue is real;
- it is outside current PR scope;
- it has clear acceptance criteria;
- delaying it does not make the current PR unsafe.

Do not use follow-ups to defer required tests, safety fixes, or architecture decisions.

## Keeping Speed Without Lowering Quality

Speed comes from bounded coherent PRs, clear acceptance criteria, reusable prompts, and
automated gates. It does not come from merging vague code and promising to clean it up
later, and it does not require splitting one coherent bounded context into many tiny PRs.

## Source And Review Archive Hygiene

Review/source archives must exclude:

- `.git/`;
- `build/`;
- `compiler/target/`;
- `target/`;
- `.sdeps/`;
- `.sloppy/`;
- local V8 SDKs;
- local binaries and PDBs;
- ZIP/7z/tar archives.

Prefer a tracked-file-only archive process after commits exist.

## Acceptance Criteria

Developer workflow is healthy when:

- each implementation PR maps to a current issue/task or explicitly states why it is
  trivial docs-only cleanup;
- gates are run and reported honestly;
- review comments distinguish blocking from non-blocking;
- follow-ups have clear scope;
- generated artifacts never enter review accidentally.
