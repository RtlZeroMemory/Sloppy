param(
    [string]$Repo = "RtlZeroMemory/Slop",
    [Alias("Input")]
    [string]$IssueInput = "tools/github/issues.json"
)

$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "common.ps1")

& (Join-Path $PSScriptRoot "validate-issue-data.ps1") -Input $IssueInput
if (-not $?) {
    throw "GitHub issue metadata validation failed."
}

$labels = Read-JsonFile (Join-Path $PSScriptRoot "labels.json")
$milestones = Read-JsonFile (Join-Path $PSScriptRoot "milestones.json")
$root = (Resolve-Path (Join-Path $PSScriptRoot "../..")).Path
$issuesPath = if ([System.IO.Path]::IsPathRooted($IssueInput)) {
    $IssueInput
} else {
    Join-Path $root $IssueInput
}
$issues = Read-JsonFile $issuesPath

function Expand-IssueTasks {
    param($Data)

    $tasks = if ($null -ne $Data.tasks) { @($Data.tasks) } else { @() }
    if ($tasks.Count -gt 0) { return $tasks }

    $generated = @()
    foreach ($epic in @($Data.epics)) {
        if ([string]::IsNullOrWhiteSpace([string]$epic.milestone) -and -not [string]::IsNullOrWhiteSpace([string]$Data.milestone)) {
            $epic | Add-Member -NotePropertyName milestone -NotePropertyValue $Data.milestone -Force
        }
        if ($null -eq $epic.suggestedTasks) { continue }
        if ($epic.title -notmatch "^EPIC-(?<number>\d+)") { continue }
        $epicNumber = $Matches.number
        $index = 0
        foreach ($taskName in @($epic.suggestedTasks)) {
            $letter = [char]([int][char]'A' + $index)
            $generated += [pscustomobject]@{
                title = "TASK $epicNumber.$letter`: $taskName"
                milestone = if (-not [string]::IsNullOrWhiteSpace([string]$epic.milestone)) { $epic.milestone } else { $Data.milestone }
            }
            $index += 1
        }
    }

    return $generated
}

foreach ($epic in @($issues.epics)) {
    if ([string]::IsNullOrWhiteSpace([string]$epic.milestone) -and -not [string]::IsNullOrWhiteSpace([string]$issues.milestone)) {
        $epic | Add-Member -NotePropertyName milestone -NotePropertyValue $issues.milestone -Force
    }
}

$tasks = Expand-IssueTasks $issues

Write-Host "GitHub dry-run summary for $Repo"
Write-Host "Issue input: $issuesPath"
Write-Host "Labels: $($labels.Count)"
Write-Host "Milestones: $($milestones.Count)"
Write-Host "EPIC issues: $($issues.epics.Count)"
Write-Host "Task issues: $($tasks.Count)"
Write-Host "Validation: passed"
Write-Host ""
Write-Host "Issue count by milestone:"
foreach ($milestone in $milestones) {
    $count = @(@($issues.epics) + @($tasks) | Where-Object { $_.milestone -eq $milestone.title }).Count
    Write-Host "  $($milestone.title): $count"
}
Write-Host ""
Write-Host "First issues by milestone:"
foreach ($milestone in $milestones) {
    $milestoneIssues = @(@($issues.epics) + @($tasks) | Where-Object { $_.milestone -eq $milestone.title } | Select-Object -First 5)
    if ($milestoneIssues.Count -eq 0) {
        continue
    }

    Write-Host "  $($milestone.title)"
    foreach ($issue in $milestoneIssues) {
        Write-Host "    - $($issue.title)"
    }
}
Write-Host ""
Write-Host "No mutation performed. Use .\tools\github\create-all.ps1 -Apply only after reviewing this summary."
