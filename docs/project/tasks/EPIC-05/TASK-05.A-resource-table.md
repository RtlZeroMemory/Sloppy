# TASK 05.A: Resource Table

## Parent EPIC
EPIC-05: Resource Lifecycle Foundation

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
Implement generation-counted resource table basics with stale ID and wrong-kind validation.

## Scope
- SlResourceId layout.
- Table insert/get/close.
- Generation increment on reuse.
- Kind checks.
- Close/reuse behavior.
- Wrong-kind and stale-ID tests.

## Non-goals
- No real database/file resources.
- No JS bridge exposure.
- No async worker integration.

## Implementation Requirements
- JS-visible resources are IDs only; stale IDs fail; double close behavior documented and tested.

## Files Likely Touched
- include/sloppy/resource.h
- src/core/resource.c
- tests/unit/core/
- docs/memory.md

## Tests Required
- stale ID; wrong kind; double close; generation reuse; invalid IDs; table exhaustion.

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
- area:resource
- area:core
- priority:p1
- risk:medium
- status:ready
- size:bounded

## Suggested PR Size
size:bounded
