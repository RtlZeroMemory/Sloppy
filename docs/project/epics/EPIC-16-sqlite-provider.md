# EPIC-16: SQLite Provider

## Milestone
0.5 Data and Capabilities

## Summary
Implement the first provider after common data and capability foundations are in place.

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
Deliver SQLite Provider as medium-sized bounded-context PRs.

## Non-goals
- No future-phase expansion.
- No unrelated runtime, compiler, provider, or package-manager work.

## Scope
- Implement the first provider after common data and capability foundations are in place.
- Each task must preserve source-doc boundaries and quality gates.

## Task Breakdown
- TASK 16.A: SQLite Provider Registration
- TASK 16.B: SQLite Query/Exec
- TASK 16.C: SQLite Transactions
- TASK 16.D: SQLite Tests/Diagnostics

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
- area:sqlite
- priority:p2
- risk:medium
- status:deferred
