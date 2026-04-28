# TASK 04.B: Source Frame / JSON Diagnostics

## Parent EPIC
EPIC-04

## Milestone
0.1 Native Core

## Source Docs
- AGENTS.md
- docs/roadmap.md
- docs/architecture.md
- docs/quality-gates.md
- adr/

## Goal
Source Frame / JSON Diagnostics as a bounded-context project task.

## Scope
- Deliver the named slice only.
- Keep the PR medium-sized and coherent.

## Non-goals
- Do not implement future-phase runtime features.
- Do not add unrelated dependencies or package-manager scope.

## Implementation Requirements
- Follow the source docs and hard boundaries.
- Update docs or ADRs if architecture changes.

## Files Likely Touched
- See EPIC file and source docs for exact paths.

## Tests Required
- Add or update relevant checks, or state why the task is spec-only.

## Quality Gates
- Run available checks before claiming completion.
- Report commands honestly.
- No generated/build artifacts staged.

## Acceptance Criteria
- Task scope is complete or intentionally spec-only.
- PR maps cleanly to this task.

## Reviewer Checklist
- Check source-doc compliance.
- Check scope creep.
- Classify findings as blocking or non-blocking.

## Follow-up Tasks
- Track deferred cleanup in docs/tech-debt-tracker.md.

## Suggested Labels
- type:task
- area:diagnostics
- area:testing
- priority:p2
- risk:medium
- status:ready
- size:bounded

## Suggested PR Size
size:bounded