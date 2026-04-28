# TASK 08.A: Handwritten app.js + app.plan Execution

## Parent EPIC
EPIC-08: Handwritten Artifact Execution

## Milestone
0.2 Runtime Contract

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
Run one synthetic handler ID from handwritten artifacts and verify a simple result.

## Scope
- Load minimal plan.
- Load handwritten JS bundle.
- Register expected handler.
- Invoke handler ID 1 with synthetic context.
- Receive simple Results.text-like descriptor.
- Validate plan/bundle consistency.

## Non-goals
- No HTTP request pipeline.
- No sloppyc fake emitter.
- No public TS API.
- No module graph.

## Implementation Requirements
- Dispatch uses numeric ID; missing/duplicate handler diagnostics tested; V8 remains isolated.

## Files Likely Touched
- tests/integration/execution/handwritten_smoke/
- src/core/runtime*
- src/engine/v8/
- src/core/plan*
- docs/execution-model.md

## Tests Required
- success result; missing handler; duplicate handler; thrown exception; wrong bundle/handler mismatch if scoped.

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
- area:plan
- area:engine-v8
- area:testing
- priority:p2
- risk:high
- status:blocked
- size:large-coherent

## Suggested PR Size
size:large-coherent
