param([string]$Repo = "RtlZeroMemory/Slop")

$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "common.ps1")

& (Join-Path $PSScriptRoot "validate-issue-data.ps1")
if (-not $?) {
    throw "GitHub issue metadata validation failed."
}

$labels = Read-JsonFile (Join-Path $PSScriptRoot "labels.json")
$milestones = Read-JsonFile (Join-Path $PSScriptRoot "milestones.json")
$issues = Read-JsonFile (Join-Path $PSScriptRoot "issues.json")

Write-Host "GitHub dry-run summary for $Repo"
Write-Host "Labels: $($labels.Count)"
Write-Host "Milestones: $($milestones.Count)"
Write-Host "EPIC issues: $($issues.epics.Count)"
Write-Host "Task issues: $($issues.tasks.Count)"
Write-Host "Validation: passed"
Write-Host ""
Write-Host "Issue count by milestone:"
foreach ($milestone in $milestones) {
    $count = @(@($issues.epics) + @($issues.tasks) | Where-Object { $_.milestone -eq $milestone.title }).Count
    Write-Host "  $($milestone.title): $count"
}
Write-Host ""
Write-Host "First issues by milestone:"
foreach ($milestone in $milestones) {
    $milestoneIssues = @(@($issues.epics) + @($issues.tasks) | Where-Object { $_.milestone -eq $milestone.title } | Select-Object -First 5)
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
