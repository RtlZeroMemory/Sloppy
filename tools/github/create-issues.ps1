param(
    [string]$Repo = "RtlZeroMemory/Slop",
    [Alias("Input")]
    [string]$IssueInput = "tools/github/issues.json",
    [switch]$Apply,
    [switch]$DryRun
)

$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "common.ps1")
$DryRun = -not $Apply
$root = (Resolve-Path (Join-Path $PSScriptRoot "../..")).Path
$issuesPath = if ([System.IO.Path]::IsPathRooted($IssueInput)) {
    $IssueInput
} else {
    Join-Path $root $IssueInput
}

if ($Apply -and $DryRun) {
    throw "Use either -Apply or -DryRun, not both."
}

& (Join-Path $PSScriptRoot "validate-issue-data.ps1") -Input $IssueInput
if (-not $?) {
    throw "GitHub issue metadata validation failed."
}

$data = Read-JsonFile $issuesPath
function Expand-IssueItems {
    param($Data)

    $expandedEpics = if ($null -ne $Data.epics) { @($Data.epics) } else { @() }
    $expandedTasks = [System.Collections.Generic.List[object]]::new()
    if ($null -ne $Data.tasks) {
        foreach ($task in @($Data.tasks)) { $expandedTasks.Add($task) | Out-Null }
    }
    foreach ($epic in $expandedEpics) {
        if ([string]::IsNullOrWhiteSpace([string]$epic.milestone) -and -not [string]::IsNullOrWhiteSpace([string]$Data.milestone)) {
            $epic | Add-Member -NotePropertyName milestone -NotePropertyValue $Data.milestone -Force
        }
    }
    if ($expandedTasks.Count -eq 0) {
        foreach ($epic in $expandedEpics) {
            if ($null -eq $epic.suggestedTasks) { continue }
            if ($epic.title -notmatch "^EPIC-(?<number>\d+)") { continue }
            $epicNumber = $Matches.number
            $index = 0
            foreach ($taskName in @($epic.suggestedTasks)) {
                $letter = [char]([int][char]'A' + $index)
                $areaLabels = @($epic.labels | Where-Object { [string]$_ -like "area:*" })
                $priority = @($epic.labels | Where-Object { -not [string]::IsNullOrWhiteSpace([string]$_) -and [string]$_ -like "priority:*" } | Select-Object -First 1)
                $risk = @($epic.labels | Where-Object { -not [string]::IsNullOrWhiteSpace([string]$_) -and [string]$_ -like "risk:*" } | Select-Object -First 1)
                $expandedTask = [pscustomobject]@{
                    title = "TASK $epicNumber.$letter`: $taskName"
                    parentEpic = $epic.title
                    labels = (@("type:task") + @($areaLabels) + @($priority) + @($risk) + @("status:ready", "size:bounded") | Where-Object { -not [string]::IsNullOrWhiteSpace([string]$_) })
                    milestone = if (-not [string]::IsNullOrWhiteSpace([string]$epic.milestone)) { $epic.milestone } else { $Data.milestone }
                    summary = "Deliver the bounded $taskName slice for $($epic.title)."
                    goal = "Complete $taskName for $($epic.title) while preserving the roadmap non-goals and source-doc boundaries."
                }
                $expandedTasks.Add($expandedTask) | Out-Null
                $index += 1
            }
        }
    }

    return @($expandedEpics) + @($expandedTasks.ToArray())
}

$items = Expand-IssueItems $data

function Join-IssueBullets {
    param($Items)

    return (@($Items) | ForEach-Object { "- $_" }) -join "`n"
}

function Get-IssueBody {
    param($Item)

    if (-not [string]::IsNullOrWhiteSpace([string]$Item.bodyPath)) {
        $bodyPath = Join-Path $root $Item.bodyPath
        if (-not (Test-Path -LiteralPath $bodyPath)) { throw "Missing issue body file: $bodyPath" }
        return Get-Content -Raw -LiteralPath $bodyPath
    }

    $lines = [System.Collections.Generic.List[string]]::new()
    $lines.Add("# $($Item.title)") | Out-Null
    $lines.Add("") | Out-Null
    if (-not [string]::IsNullOrWhiteSpace([string]$Item.parentEpic)) {
        $lines.Add("## Parent EPIC") | Out-Null
        $lines.Add([string]$Item.parentEpic) | Out-Null
        $lines.Add("") | Out-Null
    }

    $sourceDocs = if ($null -ne $Item.sourceDocs) { @($Item.sourceDocs) } else { @("AGENTS.md", "CONTRIBUTING.md", "docs/project/post-core-mvp-next-roadmap.md", "docs/project/post-core-mvp-issue-reconciliation.md", "docs/project/issue-workflow.md", "docs/project/pr-workflow.md", "docs/roadmap.md", "docs/quality-gates.md") }
    $goal = if (-not [string]::IsNullOrWhiteSpace([string]$Item.goal)) { @($Item.goal) } else { @($Item.summary) }
    $scope = if ($null -ne $Item.scope) { @($Item.scope) } elseif ($null -ne $Item.suggestedTasks) { @($Item.suggestedTasks) } else { @("Deliver the bounded issue slice described by the title and parent EPIC.", "Keep implementation aligned with docs/project/post-core-mvp-next-roadmap.md.") }
    $nonGoals = if ($null -ne $Item.nonGoals) { @($Item.nonGoals) } else { @("No unrelated runtime/compiler/provider/package-manager work.", "No future-phase behavior outside this issue.", "No generated build artifacts.") }
    $requirements = if ($null -ne $Item.implementationRequirements) { @($Item.implementationRequirements) } else { @("Implement only this bounded slice.", "Update docs/ADRs when behavior, architecture, public API, diagnostics, or workflow changes.", "Add tests that verify documented intent, not accidental current behavior.", "Report default, optional provider, and V8-gated validation separately where relevant.") }
    $files = if ($null -ne $Item.filesLikelyTouched) { @($Item.filesLikelyTouched) } else { @("docs/", "tools/", "compiler/ when compiler-scoped", "src/ and include/ only when the issue explicitly scopes runtime work", "tests/") }
    $tests = if ($null -ne $Item.testsRequired) { @($Item.testsRequired) } else { @("Task-appropriate CTest, golden/snapshot, cargo, or process tests.", "Standard Windows workflow where applicable.", "git diff --check.") }
    $qualityGates = if ($null -ne $Item.qualityGates) { @($Item.qualityGates) } else { @("Read AGENTS.md and relevant source docs before editing.", "Run available checks before claiming completion.", "Report commands honestly, including commands not run.", "No generated/build artifacts staged.", "No OS APIs outside src/platform/*.", "No V8 types outside src/engine/v8/*.", "No raw native pointers exposed to JS.") }
    $acceptance = if ($null -ne $Item.acceptanceCriteria) { @($Item.acceptanceCriteria) } else { @("The issue scope is implemented or documented as spec-only.", "Tests/checks pass or failures are reported honestly.", "Docs/ADRs are updated if behavior or architecture changes.", "The resulting PR is a bounded coherent review unit.") }
    $reviewerChecklist = if ($null -ne $Item.reviewerChecklist) { @($Item.reviewerChecklist) } else { @("Check source-doc compliance.", "Check non-goals and scope creep.", "Check tests and quality gates.", "Check platform/V8/C/Rust/JS standards boundaries as applicable.") }
    $suggestedPrSize = if (-not [string]::IsNullOrWhiteSpace([string]$Item.suggestedPrSize)) { @($Item.suggestedPrSize) } else { @("size:bounded") }
    $recommendedPrGrouping = if ($null -ne $Item.recommendedPrGrouping) { @($Item.recommendedPrGrouping) } else { @("Prefer grouping this issue with related tasks into a mid-large bounded-context PR when ownership, docs, tests, and risk are shared.") }

    foreach ($section in @(
        @("Milestone", @($Item.milestone)),
        @("Source Docs", $sourceDocs),
        @("Goal", $goal),
        @("Scope", $scope),
        @("Non-goals", $nonGoals),
        @("Implementation Requirements", $requirements),
        @("Files Likely Touched", $files),
        @("Tests Required", $tests),
        @("Quality Gates", $qualityGates),
        @("Acceptance Criteria", $acceptance),
        @("Reviewer Checklist", $reviewerChecklist),
        @("Suggested Labels", @($Item.labels)),
        @("Suggested PR Size", $suggestedPrSize),
        @("Recommended PR Grouping", $recommendedPrGrouping)
    )) {
        $heading = $section[0]
        $values = @($section[1])
        $lines.Add("## $heading") | Out-Null
        if ($values.Count -eq 1) {
            $lines.Add([string]$values[0]) | Out-Null
        } else {
            $lines.Add((Join-IssueBullets $values)) | Out-Null
        }
        $lines.Add("") | Out-Null
    }

    return (($lines -join "`n").TrimEnd() + "`n")
}

Assert-GhReady -Repo $Repo
$existingIssues = Invoke-GhJson @("issue", "list", "--repo", $Repo, "--state", "all", "--limit", "1000", "--json", "title,number,url,state")
$issuesByTitle = @{}
foreach ($issue in @($existingIssues)) {
    if (-not $issuesByTitle.ContainsKey($issue.title)) {
        $issuesByTitle[$issue.title] = @()
    }

    $issuesByTitle[$issue.title] += $issue
}

$repoLabels = Invoke-GhJson @("label", "list", "--repo", $Repo, "--limit", "1000", "--json", "name")
$repoLabelNames = [System.Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
foreach ($label in @($repoLabels)) { [void]$repoLabelNames.Add([string]$label.name) }

$repoMilestones = Invoke-GhJson @("api", "repos/$Repo/milestones", "--paginate")
$repoMilestoneNames = [System.Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
foreach ($milestone in @($repoMilestones)) { [void]$repoMilestoneNames.Add([string]$milestone.title) }

$missingLabels = [System.Collections.Generic.SortedSet[string]]::new([StringComparer]::Ordinal)
$missingMilestones = [System.Collections.Generic.SortedSet[string]]::new([StringComparer]::Ordinal)
foreach ($item in $items) {
    foreach ($label in @($item.labels)) {
        if ([string]::IsNullOrWhiteSpace([string]$label)) { continue }
        if (-not $repoLabelNames.Contains([string]$label)) { [void]$missingLabels.Add([string]$label) }
    }

    if (-not $repoMilestoneNames.Contains([string]$item.milestone)) {
        [void]$missingMilestones.Add([string]$item.milestone)
    }
}

Write-Host "Issue input: $issuesPath"
Write-Host "Repository: $Repo"
Write-Host "Mode: $(if ($DryRun) { 'dry-run' } else { 'apply' })"
if ($missingLabels.Count -gt 0) {
    Write-Host "Missing repository labels:"
    foreach ($label in $missingLabels) { Write-Host "  $label" }
}

if ($missingMilestones.Count -gt 0) {
    Write-Host "Missing repository milestones:"
    foreach ($milestone in $missingMilestones) { Write-Host "  $milestone" }
}

if ($DryRun) {
    Write-Host "Dry run: issue creation plan"
    foreach ($item in $items) {
        if ($issuesByTitle.ContainsKey($item.title)) {
            foreach ($existing in @($issuesByTitle[$item.title])) {
                Write-Host "  SKIP existing #$($existing.number) [$($existing.state)] $($item.title) -> $($existing.url)"
            }
            continue
        }

        Write-Host "  CREATE $($item.title)"
        Write-Host "    milestone: $($item.milestone)"
        Write-Host "    labels: $(@($item.labels) -join ', ')"
    }

    Write-Host "No mutation performed. Pass -Apply to create missing issues."
    return
}

if ($missingLabels.Count -gt 0 -or $missingMilestones.Count -gt 0) {
    throw "Repository is missing labels or milestones required by $IssueInput. Run the matching label/milestone workflow first, or update the input."
}

foreach ($item in $items) {
    if ($issuesByTitle.ContainsKey($item.title)) {
        foreach ($existing in @($issuesByTitle[$item.title])) {
            Write-Host "Skipping existing issue #$($existing.number) [$($existing.state)] $($item.title) -> $($existing.url)"
        }
        continue
    }
    $body = Get-IssueBody $item
    $labelArgs = @()
    foreach ($label in $item.labels) { $labelArgs += @("--label", $label) }
    Write-Host "Creating issue $($item.title)"
    & gh issue create --repo $Repo --title $item.title --body $body --milestone $item.milestone @labelArgs
    if ($LASTEXITCODE -ne 0) { throw "Failed to create issue $($item.title)" }
}
