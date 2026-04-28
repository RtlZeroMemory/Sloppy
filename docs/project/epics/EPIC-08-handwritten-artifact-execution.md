# EPIC-08: Handwritten Artifact Execution

## Milestone
0.2 Runtime Contract

## Summary
Execute handwritten app.js plus app.plan.json by numeric handler ID before HTTP or compiler extraction.

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
Prove plan/bundle consistency and handler invocation through a synthetic non-HTTP path.

## Non-goals
- No HTTP.
- No routing.
- No TypeScript compiler extraction.
- No modules or data providers.

## Scope
- Handwritten app.js and app.plan fixtures.
- Handler ID registration/invocation.
- Simple result descriptor conversion.
- Plan/bundle consistency checks.

## Task Breakdown
- TASK 08.A: Handwritten app.js + app.plan Execution

## Suggested PR Grouping
Prefer one PR per listed task. Combine only when the result remains one coherent bounded context.

## Files Likely Touched
- See task files for exact paths.
- Keep generated/build artifacts out of source control.
- Update docs/ADRs when behavior or architecture changes.

## Tests Required
- Run standard Windows workflow where applicable.
- Add or update CTest/golden/snapshot/cargo tests for implementation tasks.
- For spec-only tasks, explain why tests did not change.

## Quality Gates
- Read AGENTS.md and source docs first.
- No OS APIs outside src/platform/*.
- No V8 types outside src/engine/v8/*.
- No JS raw native pointers.
- No package-manager or Node compatibility assumptions.
- Run available gates and report commands honestly.

## Acceptance Criteria
- All listed task issues exist or are intentionally deferred.
- PRs map to bounded-context tasks.
- Docs, tests, and quality gates match the source docs.
- No future-phase implementation lands from this EPIC before prerequisites are met.

## Risks
- Scope creep into later runtime features.
- Docs and issue bodies drifting apart.
- Missing tests on implementation slices.

## Deferred Decisions
- See task follow-up sections and docs/tech-debt-tracker.md.

## Reviewer Focus
- Confirm boundaries and non-goals.
- Confirm task sizing is bounded.
- Confirm checks and tests match the risk of the slice.

## Labels
- type:epic
- area:plan
- priority:p2
- risk:high
- status:deferred
