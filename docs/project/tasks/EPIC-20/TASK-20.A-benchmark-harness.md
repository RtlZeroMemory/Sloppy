# TASK 20.A: Benchmark Harness

## Parent EPIC
EPIC-20: Benchmarks / Performance Validation

## Milestone
0.7 Tooling and Performance

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
Create the bounded Benchmark Harness slice for Benchmarks / Performance Validation.

## Scope
- Implement only the coherent Benchmark Harness slice.
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

## EPIC-20 Implementation Notes
- `sloppy_bench` is the native benchmark executable.
- `tools/windows/bench.ps1` is the documented local wrapper.
- The harness supports `--list`, `--smoke`, text output, and JSON output.
- Default CTest coverage is limited to list/smoke checks; measured benchmarks remain
  manual/local and should be run in Release for meaningful numbers.
- Output examples live under `benchmarks/fixtures/` and are explicitly sample shape, not
  performance data.

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
- area:performance
- priority:p2
- risk:medium
- status:deferred
- size:bounded

## Suggested PR Size
size:bounded
