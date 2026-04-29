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

## Alternate Issue Data

`tools/github/next-roadmap-issues.json` is staged planning data for EPIC-21 onward. The
current mutator scripts do not support an input override; `create-issues.ps1` always reads
`tools/github/issues.json`.

Do not run GitHub mutation scripts against the staged next-roadmap file by default. To
create those issues, either copy reviewed entries into the canonical issue data in a
separate PR or first add an explicit, validated `-Input` path to the tooling.

Review ZIP hygiene stays in `tools/windows/create-review-zip.ps1`, which builds archives from tracked files and excludes generated artifacts such as `.git/`, `build/`, `compiler/target/`, archives, binaries, and local dependency output. `git archive` is also acceptable for source-only review bundles.
