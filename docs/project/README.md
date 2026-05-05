# GitHub Project Model

Sloppy's docs and ADRs are the source of truth. GitHub issues mirror that source of truth; they do not replace it.

EPICs describe big outcomes. Tasks describe bounded-context PR chunks that a dev agent can implement, a reviewer agent can review, and a human architect can merge with confidence.

The preferred unit of work is one medium-sized coherent PR. Avoid micro-PR paralysis where every enum or helper becomes a separate review, and avoid kitchen-sink PRs that mix unrelated runtime, tooling, compiler, and documentation work.

Default loop:

1. Pick a ready task from `docs/project/tasks/` or its GitHub issue.
2. Generate a dev prompt from the task and source docs.
3. Dev Codex implements one bounded task.
4. Reviewer Codex reviews that task against the same docs.
5. Human architect/final reviewer consolidates feedback and decides what blocks merge.
6. Fixer Codex addresses confirmed blocking findings only.
7. Optional final verifier checks original acceptance criteria and confirmed fixes.
8. Human merges.

This project does not require a swarm of agents. The standard model is one dev Codex, one reviewer Codex, and human final review. Add extra reviewers only for high-risk slices such as V8 boundaries, allocator/resource lifetime, concurrency, provider dependencies, or security-sensitive diagnostics.

Implementation work should map to a project task. If no issue exists, create or update one before implementation unless the change is trivial docs-only cleanup.

Before applying GitHub project data, validate and dry-run it:

```powershell
.\tools\github\validate-issue-data.ps1
.\tools\github\dry-run-summary.ps1
.\tools\github\create-all.ps1
```

Only apply after validation passes and the dry-run output looks right:

```powershell
.\tools\github\create-all.ps1 -Apply
```

Do not include `.git/`, `build/`, `compiler/target/`, or other generated artifacts in review archives. Use `tools/windows/create-review-zip.ps1` or `git archive` for source-only bundles.

## Current Source-Of-Truth Reset

- `docs/project/engine-roadmap-2.md` is the current post-ENGINE-16 runtime maturation
  roadmap before the next framework expansion.
- `docs/project/engine-roadmap-2-issue-index.md` maps Roadmap-2 EPIC/TASK issues.
- `docs/project/post-engine-16-execution-model-audit.md` records the execution domain,
  async, threading, cancellation, and terminal-state audit.
- `docs/project/post-engine-16-runtime-modularity-audit.md` records the runtime feature
  composition and Plan/import/use-driven modularity audit.
- `docs/project/post-engine-16-provider-runtime-audit.md` records provider executor,
  SQLite bridge, PostgreSQL, and SQL Server runtime/offload findings.
- `docs/project/post-engine-16-http-runtime-audit.md` records HTTP maturity after
  HTTP-25 and the HTTP-26 direction.
- `docs/project/post-engine-16-lifecycle-memory-audit.md` records app/resource lifecycle
  and memory/string safety findings after ENGINE-16.
- `docs/project/post-engine-16-diagnostics-observability-audit.md` records
  diagnostics/source-map/observability findings after ENGINE-15.
- `docs/project/post-engine-16-docs-issue-reconciliation.md` records the 2026-05-05 docs
  and issue reconciliation.

## Historical Post-Core Reset

- `docs/project/post-core-mvp-code-reality-audit.md` records the compact code/test reality
  check after the Core MVP proof phase.
- `docs/project/post-core-mvp-docs-inventory.md` records which temporary docs were kept,
  archived, or deleted during the compaction.
- `docs/project/post-core-mvp-issue-reconciliation.md` records live GitHub issue/PR
  reconciliation decisions after ENGINE-19 and ENGINE-24.
- `docs/project/post-core-mvp-memory-string-audit.md` records remaining primitive adoption
  gaps without reopening broad runtime rewrites.
- `docs/project/post-core-mvp-boundary-audit.md` records V8/libuv/provider/HTTP boundary
  findings.
- `docs/project/post-core-mvp-next-roadmap.md` proposes the next wave. It is proposal-only;
  the owner-approved post-Core issue wave is now mapped in
  `docs/project/post-core-next-wave-issue-map.md`.
- `docs/project/framework-app-layer-roadmap.md` records the FRAMEWORK-01 source of truth.
- `docs/project/framework-api-shape.md` locks the post-Core framework/API ergonomics and
  Plan-first design target before implementation.
- `docs/project/source-input-run-dev-loop-plan.md` records the reused source-input/dev-loop
  plan for #259/#302/#346 and the remaining #316/#345/#349 dev-loop work.
- `docs/project/strong-plan-strategic-layer-plan.md` records the reused Strong Plan plan
  for #318/#355-#359.
- `docs/project/http-post-mvp-transport-plan.md` records the HTTP-25 post-MVP transport
  plan.
- `docs/project/post-core-immediate-hardening-plan.md` records the HARDEN-01 and reused
  hardening issue map.
