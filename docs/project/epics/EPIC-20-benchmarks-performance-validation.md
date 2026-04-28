# EPIC-20: Benchmarks / Performance Validation

## Milestone
0.7 Tooling and Performance

## Summary
Add benchmark harness and validation for route matcher, handler dispatch, and later HTTP/JSON/DB claims.

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
Deliver Benchmarks / Performance Validation as medium-sized bounded-context PRs.

## Non-goals
- No future-phase expansion.
- No unrelated runtime, compiler, provider, or package-manager work.

## Scope
- Add benchmark harness and validation for route matcher, handler dispatch, and later HTTP/JSON/DB claims.
- Each task must preserve source-doc boundaries and quality gates.

## Task Breakdown
- TASK 20.A: Benchmark Harness
- TASK 20.B: Route Matcher Benchmarks
- TASK 20.C: Handler Dispatch Benchmarks
- TASK 20.D: HTTP/JSON/DB Benchmarks Later

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
- area:performance
- priority:p2
- risk:medium
- status:deferred
