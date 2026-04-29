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

## Post-0.7 Planning

- `docs/project/post-0.7-issue-audit.md` records the recommended cleanup for stale/open
  EPIC-00 through EPIC-20 GitHub issues after the initial roadmap batch.
- `docs/project/next-roadmap.md` defines the proposed EPIC-21 through EPIC-28 roadmap.
- `tools/github/next-roadmap-issues.json` is staged data only. Current GitHub creation
  scripts do not support an alternate input file, so do not mutate GitHub from it by
  default.
