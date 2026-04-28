# TASK 17.A: libpq Provider Skeleton

## Parent EPIC
EPIC-17: PostgreSQL Provider

## Milestone
0.6 External Providers

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
Create the bounded libpq Provider Skeleton slice for PostgreSQL Provider.

## Scope
- Implement only the coherent libpq Provider Skeleton slice.
- Keep behavior aligned with source docs and prerequisites.

## Non-goals
- Do not pull future-phase work into this task.
- Do not add dependencies before the relevant phase/docs.

## Implementation Requirements
- Add tests or fixtures appropriate to the slice; update docs/ADRs if architecture changes.

## Files Likely Touched
- docs/
- src/
- include/
- tests/
- compiler/ when explicitly scoped

## Tests Required
- Relevant CTest, golden/snapshot, or cargo tests depending on touched files.

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
- type:task
- area:postgres
- priority:p2
- risk:high
- status:deferred
- size:bounded

## Suggested PR Size
size:bounded
