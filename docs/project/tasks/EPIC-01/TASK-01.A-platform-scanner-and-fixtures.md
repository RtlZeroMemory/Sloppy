# TASK 01.A: Platform Scanner and Fixtures

## Parent EPIC
EPIC-01: Platform Abstraction

## Milestone
0.1 Native Core

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
- docs/developer-ergonomics.md
- docs/modularity.md
- docs/data-providers.md
- docs/testing.md
- docs/quality-gates.md
- docs/agent-harness.md
- docs/review-playbook.md
- docs/skills/README.md
- docs/quality-score.md
- docs/tech-debt-tracker.md
- adr/

## Goal
Prove forbidden OS headers and direct platform leakage are caught mechanically.

## Scope
- Add scanner self-test or fixtures.
- Keep allowed platform dirs explicit.

## Non-goals
- No OS API implementation.
- No broad lint rewrite.

## Implementation Requirements
- Failure output names offending file/header.

## Files Likely Touched
- tools/windows/check-platform-boundaries.ps1
- tools/windows/check-c-standards.ps1
- tests or tool fixtures
- docs/platform-abstraction.md

## Tests Required
- lint gate; scanner fixture/self-test if added.

## Quality Gates
- Read AGENTS.md and relevant source docs before editing.
- Run available checks before claiming completion.
- Report commands honestly, including commands not run.
- No generated/build artifacts staged.
- No OS APIs outside src/platform/*.
- No V8 types outside src/engine/v8/*.
- No raw native pointers exposed to JS.
- No future-phase implementation unless explicitly scoped.

## Acceptance Criteria
- The task scope is implemented or documented as spec-only.
- Tests/checks listed above pass or failures are reported honestly.
- Docs/ADRs are updated if behavior or architecture changes.
- The resulting PR is a bounded coherent review unit.

## Reviewer Checklist
- Check source-doc compliance.
- Check non-goals and scope creep.
- Check tests and quality gates.
- Check platform/V8/C standards boundaries.
- Classify findings as blocking or non-blocking.

## Follow-up Tasks
- Convert non-blocking review ideas into follow-up issues when useful.
- Track deferred cleanup in docs/tech-debt-tracker.md.

## Suggested Labels
- type:tooling
- area:platform
- area:ci
- priority:p1
- risk:medium
- status:ready
- size:bounded

## Suggested PR Size
size:bounded
