# GitHub Project Scripts

These scripts create the GitHub ceremony data for `RtlZeroMemory/Slop` from local JSON and markdown files.

Prerequisites:

- `gh` installed and on `PATH`.
- `gh auth login` completed.
- Repository access to `RtlZeroMemory/Slop`.

Dry run, default:

```powershell
.\tools\github\validate-issue-data.ps1
.\tools\github\dry-run-summary.ps1
.\tools\github\create-all.ps1
```

Apply changes:

```powershell
.\tools\github\create-all.ps1 -Apply
```

Validation runs before issue creation, including dry-run mode. `create-all.ps1` also runs
validation before creating labels or milestones, so invalid issue metadata stops the whole
apply path before any GitHub mutation occurs.

The scripts do not delete existing labels, milestones, issues, or repository settings. They create missing labels/milestones/issues and skip or update only where explicitly supported. Issue duplicate detection checks open issues by exact title and may miss renamed or closed duplicates.

Review ZIP hygiene stays in `tools/windows/create-review-zip.ps1`, which builds archives from tracked files and excludes generated artifacts such as `.git/`, `build/`, `compiler/target/`, archives, binaries, and local dependency output. `git archive` is also acceptable for source-only review bundles.
