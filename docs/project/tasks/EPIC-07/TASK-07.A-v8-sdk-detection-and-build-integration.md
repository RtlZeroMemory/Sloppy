# TASK 07.A: V8 SDK Detection and Build Integration

## Parent EPIC
EPIC-07: V8 Bridge Smoke

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
Add phase-gated V8 SDK discovery without making non-V8 builds fail unexpectedly.

## Scope
- SLOPPY_V8_ROOT or documented SDK layout.
- CMake options for V8-enabled bridge.
- Clear diagnostics for missing SDK.

## Non-goals
- No V8 runtime code outside src/engine/v8.
- No bundling/packaging promises.

## Implementation Requirements
- Build without V8 remains possible where documented; setup instructions are clear.

## Files Likely Touched
- src/engine/v8/
- CMakeLists.txt
- tools/windows/fetch-v8.ps1
- docs/dependencies.md

## Tests Required
- configure without V8; missing SDK diagnostic; build gate where available.

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
- area:engine-v8
- area:ci
- priority:p2
- risk:high
- status:blocked
- size:bounded

## Suggested PR Size
size:bounded
