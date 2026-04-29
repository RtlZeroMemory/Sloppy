# Issue Workflow

1. Pick the next ready task.

   Prefer `status:ready` tasks in the next milestone. Confirm prerequisites and source docs before implementation starts.

Before creating GitHub issues from local metadata, run:

```powershell
.\tools\github\validate-issue-data.ps1
.\tools\github\dry-run-summary.ps1
.\tools\github\create-all.ps1
```

For reviewed alternate issue data, pass the input file explicitly:

```powershell
.\tools\github\validate-issue-data.ps1 -Input tools/github/next-roadmap-issues.json
.\tools\github\dry-run-summary.ps1 -Input tools/github/next-roadmap-issues.json
.\tools\github\create-issues.ps1 -Input tools/github/next-roadmap-issues.json -DryRun
```

Apply only after validation passes and the dry run is reviewed:

```powershell
.\tools\github\create-all.ps1 -Apply
```

For alternate data, apply only after the input-specific dry run is reviewed:

```powershell
.\tools\github\create-all.ps1 -Input tools/github/next-roadmap-issues.json -Apply
```

Issue creation must skip exact-title matches and must not close, relabel, or update existing
issues unless a future task explicitly asks for that mutation.

2. Generate a dev prompt from the task.

   Use `docs/project/prompts/dev-prompt-template.md`. Include repo, branch, issue link, source docs, scope, non-goals, tests, quality gates, and commands.

3. Dev Codex opens a PR.

   The PR should deliver one bounded context. It should not add future-phase work or unrelated cleanup.

4. Reviewer Codex reviews against the task and docs.

   Use `docs/project/prompts/reviewer-prompt-template.md`. Findings must be concise, actionable, and classified as blocking or non-blocking.

5. Human consolidates feedback.

   The human architect decides which findings block merge and which become follow-up issues.

6. Dev Codex fixes.

   Use `docs/project/prompts/fixer-prompt-template.md`. Fix confirmed blocking findings only and avoid scope expansion.

7. Optional final verifier.

   Use `docs/project/prompts/final-verifier-prompt-template.md` for high-risk or large-coherent PRs.

8. Merge.

   Merge only after acceptance criteria, required checks, and blocking feedback are satisfied or explicitly documented.

9. Update issue and tech-debt tracker if needed.

   Close completed tasks, link follow-up issues, and record deferred cleanup in `docs/tech-debt-tracker.md`.

Review bundles should be source-only. Use `tools/windows/create-review-zip.ps1` or `git archive`; do not include `.git/`, `build/`, `compiler/target/`, or generated local artifacts.
