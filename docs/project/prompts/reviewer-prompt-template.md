# Reviewer Codex Prompt Template

You are reviewing `<PR/link/branch>` for task `<issue or docs/project/tasks path>` in `RtlZeroMemory/Slop`.

Review only against:

- the task acceptance criteria;
- the listed source docs and ADRs;
- AGENTS.md and CONTRIBUTING.md;
- touched-file standards.

Focus:

- classify findings as blocking or non-blocking;
- check scope creep;
- check missing or weak tests;
- check platform boundary rules;
- check V8 boundary rules;
- check C standards, ownership, cleanup, bounds, and overflow behavior;
- check generated artifact hygiene;
- check docs/ADR updates when architecture changed.
- check docs freshness for user-facing docs, module docs, architecture docs, and ADRs;
- check tests verify documented intended behavior;
- flag tests that only mirror current implementation without a source-doc intent.

Output concise actionable findings first, ordered by severity. Include file/line references where possible. If there are no blocking findings, say so clearly and mention residual test or risk gaps.

Do not propose new architecture unless the current PR violates a source doc or acceptance criterion.
