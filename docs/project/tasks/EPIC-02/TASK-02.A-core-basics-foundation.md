# TASK 02.A: Core Basics Foundation

## Parent EPIC
EPIC-02: Core Native Foundation

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
Implement the first coherent core primitive layer and test harness as one bounded foundation PR.

## Scope
- SlStatus and SlStatusCode with helpers.
- SlSourceLoc unknown/default helpers.
- SlStr and SlBytes borrowed views.
- Checked size_t add/mul.
- Assertion macros.
- C unit test harness in CMake/CTest.

## Non-goals
- No arena allocator.
- No SlBuf/StringBuilder unless split into EPIC-03.
- No diagnostics renderer.
- No resource table.
- No V8, HTTP, OS APIs, or compiler expansion.

## Implementation Requirements
- Headers self-contained and document ownership/lifetimes.
- Public symbols use sl_ and SlTypeName.
- No strlen-driven internal logic except boundary adapters.
- Overflow behavior tested.
- CTest names expose subsystem behavior.

## Files Likely Touched
- include/sloppy/status.h
- include/sloppy/source_loc.h
- include/sloppy/string.h
- include/sloppy/bytes.h
- include/sloppy/checked_math.h
- include/sloppy/assert.h
- src/core/
- tests/unit/core/
- CMakeLists.txt

## Tests Required
- status success/failure; source loc defaults; SlStr empty/nonempty/embedded NUL; SlBytes zero length; checked add/mul overflow; assertion behavior where testable; CTest registration.

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
- area:core
- area:testing
- priority:p0
- risk:medium
- status:ready
- size:bounded

## Suggested PR Size
size:bounded
