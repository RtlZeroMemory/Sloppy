param(
    [string]$Repo = "RtlZeroMemory/Slop",
    [Alias("Input")]
    [string]$IssueInput = "tools/github/issues.json",
    [switch]$Apply,
    [switch]$ForceLabels
)

$ErrorActionPreference = "Stop"
$commonParams = @{
    Repo = $Repo
}
if ($Apply) { $commonParams.Apply = $true }

$labelParams = @{}
foreach ($key in $commonParams.Keys) { $labelParams[$key] = $commonParams[$key] }
if ($ForceLabels) { $labelParams.Force = $true }

& (Join-Path $PSScriptRoot "validate-issue-data.ps1") -Input $IssueInput
if (-not $?) {
    throw "GitHub issue metadata validation failed. No GitHub data was changed."
}

& (Join-Path $PSScriptRoot "create-labels.ps1") @labelParams
& (Join-Path $PSScriptRoot "create-milestones.ps1") @commonParams
& (Join-Path $PSScriptRoot "create-issues.ps1") @commonParams -Input $IssueInput

if (-not $Apply) { Write-Host "Dry run complete. Pass -Apply to mutate GitHub." }
